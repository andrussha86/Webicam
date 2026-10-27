import AVFoundation
import WebicamCore

enum CaptureError: Error {
    case noCamera
    case unsupportedFormat
}

/// Selected lens → NV12 (420v) pixel buffers with host-clock timestamps, with the PC's
/// manual controls and optional background replacement applied.
final class CaptureEngine: NSObject, AVCaptureVideoDataOutputSampleBufferDelegate {
    var onFrame: ((CVPixelBuffer, CMTime) -> Void)?
    /// The background picture cannot be shown; called on the capture queue.
    var onBackgroundError: (() -> Void)? {
        get { compositor.onImageError }
        set { compositor.onImageError = newValue }
    }

    private let session = AVCaptureSession()
    private let output = AVCaptureVideoDataOutput()
    private let queue = DispatchQueue(label: "webicam.capture", qos: .userInteractive)
    private let compositor = BackgroundCompositor()
    private var configured = false
    // Owned by `queue`.
    private var input: AVCaptureDeviceInput?
    private var format: (width: Int, height: Int, fps: Int)?
    private var settings = CameraSettings()
    private var activeLens: Lens?

    private static let sizes: [(Int32, Int32)] = [(1280, 720), (1920, 1080), (3840, 2160)]
    private static let pixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange
    /// Digital zoom allowed on top of a lens, beyond that the picture is mush.
    private static let maxDigitalZoom: CGFloat = 5

    static func device(for lens: Lens) -> AVCaptureDevice? {
        switch lens {
        case .ultraWide: return AVCaptureDevice.default(.builtInUltraWideCamera, for: .video, position: .back)
        case .wide, .wide2x: return AVCaptureDevice.default(.builtInWideAngleCamera, for: .video, position: .back)
        case .telephoto: return AVCaptureDevice.default(.builtInTelephotoCamera, for: .video, position: .back)
        case .front: return AVCaptureDevice.default(.builtInWideAngleCamera, for: .video, position: .front)
        }
    }

    /// Bit i set = Lens(rawValue: i) exists on this phone.
    static let availableLenses: UInt8 = Lens.allCases.reduce(0) { mask, lens in
        device(for: lens) != nil ? mask | (1 << lens.rawValue) : mask
    }

    /// Supported 720p/1080p/4K formats of the main camera with their highest frame rate (≤ 60).
    var supportedFormats: [CaptureFormat] {
        guard let device = Self.device(for: .wide) else { return [] }
        var best: [String: CaptureFormat] = [:]
        for format in device.formats where Self.isUsable(format) {
            let d = CMVideoFormatDescriptionGetDimensions(format.formatDescription)
            let fps = UInt8(min(60, Self.maxFps(format)))
            let key = "\(d.width)x\(d.height)"
            if fps > (best[key]?.maxFps ?? 0) {
                best[key] = CaptureFormat(width: UInt16(d.width), height: UInt16(d.height), maxFps: fps)
            }
        }
        return best.values.sorted { $0.width < $1.width }
    }

    private static func isUsable(_ format: AVCaptureDevice.Format) -> Bool {
        let d = CMVideoFormatDescriptionGetDimensions(format.formatDescription)
        guard sizes.contains(where: { $0 == (d.width, d.height) }) else { return false }
        return CMFormatDescriptionGetMediaSubType(format.formatDescription) == pixelFormat
    }

    private static func maxFps(_ format: AVCaptureDevice.Format) -> Double {
        format.videoSupportedFrameRateRanges.map(\.maxFrameRate).max() ?? 0
    }

    /// Sets the stream format on the current lens. Returns the frame rate actually used:
    /// a lens that can't reach `fps` at this size runs at its own maximum.
    @discardableResult
    func configure(width: Int, height: Int, fps: Int) throws -> Int {
        try queue.sync {
            let previous = format
            format = (width, height, fps)
            do {
                return try activate(lens: settings.lens)
            } catch {
                format = previous
                throw error
            }
        }
    }

    /// Applies the PC's snapshot. A lens that can't be used keeps the previous one and throws.
    func apply(_ new: CameraSettings) throws {
        try queue.sync {
            let previousLens = settings.lens
            if new.backgroundMode != settings.backgroundMode { compositor.reset() }
            settings = new
            guard format != nil else { return }
            if Self.device(for: new.lens)?.uniqueID != input?.device.uniqueID || activeLens == nil {
                do {
                    _ = try activate(lens: new.lens)
                } catch {
                    settings.lens = previousLens
                    throw error
                }
            } else {
                activeLens = new.lens
                try updateControls()
            }
        }
    }

    /// The PC's picture for image mode.
    func setBackgroundImage(_ jpeg: Data) {
        queue.async { [compositor] in compositor.setImage(jpeg) }
    }

    /// Ranges of the active lens and its current values; nil before the first Configure.
    func state() -> CameraState? {
        queue.sync {
            guard let device = input?.device, let activeLens else { return nil }
            let format = device.activeFormat
            var s = CameraState()
            s.lenses = Self.availableLenses
            s.lens = activeLens
            s.isoMin = format.minISO
            s.isoMax = format.maxISO
            s.shutterMinUs = Self.microseconds(format.minExposureDuration)
            s.shutterMaxUs = Self.microseconds(Self.longestExposure(device))
            s.biasMin = device.minExposureTargetBias
            s.biasMax = device.maxExposureTargetBias
            s.zoomMax = Float(Self.zoomLimit(device, activeLens) / Self.baseZoom(activeLens))
            s.manualFocusSupported = device.isLockingFocusWithCustomLensPositionSupported
            s.iso = device.iso
            s.shutterUs = Self.microseconds(device.exposureDuration)
            let wb = device.temperatureAndTintValues(for: device.deviceWhiteBalanceGains)
            s.temperatureK = wb.temperature
            s.tint = wb.tint
            s.lensPosition = device.lensPosition
            s.reactionsSupported = format.reactionEffectsSupported
            s.reactionsEnabled = AVCaptureDevice.reactionEffectsEnabled
            s.gesturesEnabled = AVCaptureDevice.reactionEffectGesturesEnabled
            return s
        }
    }

    func start() {
        queue.async { [session] in
            if !session.isRunning { session.startRunning() }
        }
    }

    func stop() {
        queue.async { [session] in
            if session.isRunning { session.stopRunning() }
        }
    }

    func captureOutput(_ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer,
                       from connection: AVCaptureConnection) {
        guard let pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer) else { return }
        var frame = pixelBuffer
        if let composed = compositor.process(pixelBuffer, settings: settings) {
            frame = composed
        }
        onFrame?(frame, CMSampleBufferGetPresentationTimeStamp(sampleBuffer))
    }

    // MARK: - On `queue`

    /// Switches the session to `lens` with the current format and re-applies every control.
    private func activate(lens: Lens) throws -> Int {
        guard let current = format else { return 0 }
        let (width, height, fps) = current
        guard let device = Self.device(for: lens) else { throw CaptureError.noCamera }
        let candidates = device.formats.filter { format in
            let d = CMVideoFormatDescriptionGetDimensions(format.formatDescription)
            return Self.isUsable(format) && Int(d.width) == width && Int(d.height) == height
        }
        guard let chosen = candidates.first(where: { Self.maxFps($0) >= Double(fps) })
            ?? candidates.max(by: { Self.maxFps($0) < Self.maxFps($1) })
        else { throw CaptureError.unsupportedFormat }
        let effectiveFps = min(fps, Int(Self.maxFps(chosen)))

        session.beginConfiguration()
        defer { session.commitConfiguration() }
        if !configured {
            session.sessionPreset = .inputPriority
            output.alwaysDiscardsLateVideoFrames = true
            output.videoSettings = [kCVPixelBufferPixelFormatTypeKey as String: Self.pixelFormat]
            output.setSampleBufferDelegate(self, queue: queue)
            if session.canAddOutput(output) { session.addOutput(output) }
            configured = true
        }
        if input?.device.uniqueID != device.uniqueID {
            let newInput = try AVCaptureDeviceInput(device: device)
            if let input { session.removeInput(input) }
            guard session.canAddInput(newInput) else {
                if let input { session.addInput(input) }
                throw CaptureError.noCamera
            }
            session.addInput(newInput)
            input = newInput
            compositor.reset()
        }
        try device.lockForConfiguration()
        device.activeFormat = chosen
        let duration = CMTime(value: 1, timescale: CMTimeScale(effectiveFps))
        device.activeVideoMinFrameDuration = duration
        device.activeVideoMaxFrameDuration = duration
        activeLens = lens
        applyControls(device)
        device.unlockForConfiguration()
        applyRotation()
        return effectiveFps
    }

    private func updateControls() throws {
        guard let device = input?.device else { return }
        try device.lockForConfiguration()
        applyControls(device)
        device.unlockForConfiguration()
        applyRotation()
    }

    /// Zoom, exposure, white balance and focus from `settings`, clamped to what the lens allows.
    /// The device must be locked for configuration.
    private func applyControls(_ device: AVCaptureDevice) {
        let s = settings
        let format = device.activeFormat

        let zoom = Self.baseZoom(s.lens) * CGFloat(max(s.zoom, 1))
        device.videoZoomFactor = min(max(zoom, device.minAvailableVideoZoomFactor), Self.zoomLimit(device, s.lens))

        if s.manualExposure && device.isExposureModeSupported(.custom) {
            let requested = CMTime(value: CMTimeValue(s.shutterUs), timescale: 1_000_000)
            let duration = CMTimeMinimum(CMTimeMaximum(requested, format.minExposureDuration),
                                         Self.longestExposure(device))
            let iso = min(max(s.iso, format.minISO), format.maxISO)
            device.setExposureModeCustom(duration: duration, iso: iso, completionHandler: nil)
        } else {
            if device.isExposureModeSupported(.continuousAutoExposure) {
                device.exposureMode = .continuousAutoExposure
            }
            let bias = min(max(s.exposureBias, device.minExposureTargetBias), device.maxExposureTargetBias)
            device.setExposureTargetBias(bias, completionHandler: nil)
        }

        if s.manualWhiteBalance && device.isLockingWhiteBalanceWithCustomDeviceGainsSupported {
            let values = AVCaptureDevice.WhiteBalanceTemperatureAndTintValues(temperature: s.temperatureK, tint: s.tint)
            var gains = device.deviceWhiteBalanceGains(for: values)
            let maxGain = device.maxWhiteBalanceGain
            gains.redGain = min(max(gains.redGain, 1), maxGain)
            gains.greenGain = min(max(gains.greenGain, 1), maxGain)
            gains.blueGain = min(max(gains.blueGain, 1), maxGain)
            device.setWhiteBalanceModeLocked(with: gains, completionHandler: nil)
        } else if device.isWhiteBalanceModeSupported(.continuousAutoWhiteBalance) {
            device.whiteBalanceMode = .continuousAutoWhiteBalance
        }

        if s.manualFocus && device.isLockingFocusWithCustomLensPositionSupported {
            device.setFocusModeLocked(lensPosition: min(max(s.lensPosition, 0), 1), completionHandler: nil)
        } else if device.isFocusModeSupported(.continuousAutoFocus) {
            device.focusMode = .continuousAutoFocus
        }

        // Face-driven focus and exposure, set explicitly so the system never decides on its own;
        // they only matter while the parameter is on auto.
        device.automaticallyAdjustsFaceDrivenAutoFocusEnabled = false
        device.isFaceDrivenAutoFocusEnabled = s.faceFocus && !s.manualFocus
        device.automaticallyAdjustsFaceDrivenAutoExposureEnabled = false
        device.isFaceDrivenAutoExposureEnabled = s.faceFocus && !s.manualExposure
    }

    private func applyRotation() {
        let angle: CGFloat = settings.rotate180 ? 180 : 0  // landscape, sensor-native
        if let connection = output.connection(with: .video), connection.isVideoRotationAngleSupported(angle) {
            connection.videoRotationAngle = angle
        }
    }

    /// 2× is the main camera's 48 MP sensor cropped, everything else starts at 1×.
    private static func baseZoom(_ lens: Lens) -> CGFloat { lens == .wide2x ? 2 : 1 }

    private static func zoomLimit(_ device: AVCaptureDevice, _ lens: Lens) -> CGFloat {
        min(device.maxAvailableVideoZoomFactor, baseZoom(lens) * maxDigitalZoom)
    }

    /// A manual exposure can't be longer than one frame without lowering the frame rate.
    private static func longestExposure(_ device: AVCaptureDevice) -> CMTime {
        CMTimeMinimum(device.activeFormat.maxExposureDuration, device.activeVideoMinFrameDuration)
    }

    private static func microseconds(_ time: CMTime) -> UInt32 {
        UInt32(max(0, min(Double(UInt32.max), CMTimeGetSeconds(time) * 1_000_000)))
    }
}

/// Microseconds on the host clock — the clock capture timestamps use.
func hostTimeUs() -> UInt64 {
    UInt64(max(0, CMTimeGetSeconds(CMClockGetTime(CMClockGetHostTimeClock())) * 1_000_000))
}

func microseconds(_ time: CMTime) -> UInt64 {
    UInt64(max(0, CMTimeGetSeconds(time) * 1_000_000))
}
