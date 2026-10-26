import AVFoundation
import WebicamCore

enum CaptureError: Error {
    case noCamera
    case unsupportedFormat
}

/// Back wide-angle camera → NV12 (420v) pixel buffers with host-clock timestamps.
final class CaptureEngine: NSObject, AVCaptureVideoDataOutputSampleBufferDelegate {
    var onFrame: ((CVPixelBuffer, CMTime) -> Void)?

    private let session = AVCaptureSession()
    private let output = AVCaptureVideoDataOutput()
    private let queue = DispatchQueue(label: "webicam.capture", qos: .userInteractive)
    private let device = AVCaptureDevice.default(.builtInWideAngleCamera, for: .video, position: .back)
    private var configured = false

    private static let sizes: [(Int32, Int32)] = [(1280, 720), (1920, 1080), (3840, 2160)]
    private static let pixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange

    /// Supported 720p/1080p/4K formats with their highest frame rate (≤ 60).
    var supportedFormats: [CaptureFormat] {
        guard let device else { return [] }
        var best: [String: CaptureFormat] = [:]
        for format in device.formats where Self.isUsable(format) {
            let d = CMVideoFormatDescriptionGetDimensions(format.formatDescription)
            let fps = UInt8(min(60, format.videoSupportedFrameRateRanges.map(\.maxFrameRate).max() ?? 0))
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

    func configure(width: Int, height: Int, fps: Int) throws {
        guard let device else { throw CaptureError.noCamera }
        let candidates = device.formats.filter { format in
            let d = CMVideoFormatDescriptionGetDimensions(format.formatDescription)
            return Self.isUsable(format) && Int(d.width) == width && Int(d.height) == height
                && format.videoSupportedFrameRateRanges.contains { $0.maxFrameRate >= Double(fps) }
        }
        guard let format = candidates.first else { throw CaptureError.unsupportedFormat }

        try queue.sync {
            session.beginConfiguration()
            defer { session.commitConfiguration() }
            if !configured {
                session.sessionPreset = .inputPriority
                let input = try AVCaptureDeviceInput(device: device)
                if session.canAddInput(input) { session.addInput(input) }
                output.alwaysDiscardsLateVideoFrames = true
                output.videoSettings = [kCVPixelBufferPixelFormatTypeKey as String: Self.pixelFormat]
                output.setSampleBufferDelegate(self, queue: queue)
                if session.canAddOutput(output) { session.addOutput(output) }
                configured = true
            }
            try device.lockForConfiguration()
            device.activeFormat = format
            let duration = CMTime(value: 1, timescale: CMTimeScale(fps))
            device.activeVideoMinFrameDuration = duration
            device.activeVideoMaxFrameDuration = duration
            device.unlockForConfiguration()
            if let connection = output.connection(with: .video), connection.isVideoRotationAngleSupported(0) {
                connection.videoRotationAngle = 0  // landscape, sensor-native
            }
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
        onFrame?(pixelBuffer, CMSampleBufferGetPresentationTimeStamp(sampleBuffer))
    }
}

/// Microseconds on the host clock — the clock capture timestamps use.
func hostTimeUs() -> UInt64 {
    UInt64(max(0, CMTimeGetSeconds(CMClockGetTime(CMClockGetHostTimeClock())) * 1_000_000))
}

func microseconds(_ time: CMTime) -> UInt64 {
    UInt64(max(0, CMTimeGetSeconds(time) * 1_000_000))
}
