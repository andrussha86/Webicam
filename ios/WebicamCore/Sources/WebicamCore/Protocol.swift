import Foundation

/// Webicam wire protocol v4 — see shared/protocol.md.
public enum ProtocolConstants {
    public static let version: UInt16 = 4
    public static let phonePort: UInt16 = 47800
    public static let maxPayloadBytes = 16 * 1024 * 1024
}

public struct CaptureFormat: Equatable, Hashable {
    public var width: UInt16
    public var height: UInt16
    public var maxFps: UInt8

    public init(width: UInt16, height: UInt16, maxFps: UInt8) {
        self.width = width
        self.height = height
        self.maxFps = maxFps
    }
}

public enum StreamState: UInt8 {
    case streaming = 0
    case stoppedByUser = 1
    case thermalThrottled = 2
    case error = 3
}

public enum Lens: UInt8, CaseIterable {
    case ultraWide = 0, wide, wide2x, telephoto, front
}

public enum BackgroundMode: UInt8 {
    case off = 0, blur, image, color
}

/// Auto follows how the phone is held, but only ever turns the picture by 180°.
public enum Rotation: UInt8 {
    case auto = 0, upright, flipped
}

/// Full snapshot of the PC's camera choices; the phone clamps every value to the active lens.
public struct CameraSettings: Equatable {
    public var lens: Lens = .wide
    public var zoom: Float = 1  // >= 1, multiplier within the lens
    public var manualExposure = false
    public var iso: Float = 100
    public var shutterUs: UInt32 = 10000
    public var exposureBias: Float = 0  // EV, used in auto exposure
    public var manualWhiteBalance = false
    public var temperatureK: Float = 5000
    public var tint: Float = 0
    public var manualFocus = false
    public var lensPosition: Float = 0.5  // 0 near ... 1 far
    public var blur: UInt8 = 0  // 0 off ... 100 strongest
    public var rotation: Rotation = .auto
    public var faceFocus = false  // face-driven focus/exposure while those are on auto
    public var backgroundMode: BackgroundMode = .off
    public var backgroundColor: UInt32 = 0x1F2125  // 0x00RRGGBB
    public var mirrorHorizontal = false
    public var mirrorVertical = false
    /// Centre of the zoomed area in the output frame, 0...1 from the top left.
    public var panX: Float = 0.5
    public var panY: Float = 0.5

    public init() {}
}

/// Ranges of the active lens and its current (auto or manual) values.
public struct CameraState: Equatable {
    public var lenses: UInt8 = 0  // bit i set = Lens(rawValue: i) is available
    public var lens: Lens = .wide
    public var isoMin: Float = 0
    public var isoMax: Float = 0
    public var shutterMinUs: UInt32 = 0
    public var shutterMaxUs: UInt32 = 0
    public var biasMin: Float = 0
    public var biasMax: Float = 0
    public var zoomMax: Float = 1
    public var manualFocusSupported = false
    public var iso: Float = 0
    public var shutterUs: UInt32 = 0
    public var temperatureK: Float = 0
    public var tint: Float = 0
    public var lensPosition: Float = 0
    public var reactionsSupported = false  // by the active format
    public var reactionsEnabled = false  // Control Center, Video Effects
    public var gesturesEnabled = false
    public var flipped = false  // the picture is turned by 180° right now

    public init() {}
}

enum MessageType: UInt8 {
    case hello = 1, configure, videoFrame, requestKeyframe, ping, pong, status, cameraSettings, cameraState, backgroundImage
}

public enum Message: Equatable {
    /// The PC sends only `version` (model nil, formats empty).
    case hello(version: UInt16, model: String?, formats: [CaptureFormat])
    case configure(width: UInt16, height: UInt16, fps: UInt8, bitrateKbps: UInt32)
    case videoFrame(captureTimeUs: UInt64, keyframe: Bool, annexB: Data)
    case requestKeyframe
    case ping(pcTimeUs: UInt64)
    case pong(pcTimeUs: UInt64, phoneTimeUs: UInt64)
    case status(state: StreamState, thermal: UInt8, message: String)
    case cameraSettings(CameraSettings)
    case cameraState(CameraState)
    /// JPEG for BackgroundMode.image; empty = no picture.
    case backgroundImage(Data)

    public func encoded() -> Data {
        var payload = ByteWriter()
        let type: MessageType
        switch self {
        case let .hello(version, model, formats):
            type = .hello
            payload.u16(version)
            if model != nil || !formats.isEmpty {
                payload.str(model ?? "")
                payload.u16(UInt16(formats.count))
                for format in formats {
                    payload.u16(format.width)
                    payload.u16(format.height)
                    payload.u8(format.maxFps)
                }
            }
        case let .configure(width, height, fps, bitrateKbps):
            type = .configure
            payload.u16(width)
            payload.u16(height)
            payload.u8(fps)
            payload.u32(bitrateKbps)
        case let .videoFrame(captureTimeUs, keyframe, annexB):
            type = .videoFrame
            payload.u64(captureTimeUs)
            payload.u8(keyframe ? 1 : 0)
            payload.bytes.append(contentsOf: annexB)
        case .requestKeyframe:
            type = .requestKeyframe
        case let .ping(pcTimeUs):
            type = .ping
            payload.u64(pcTimeUs)
        case let .pong(pcTimeUs, phoneTimeUs):
            type = .pong
            payload.u64(pcTimeUs)
            payload.u64(phoneTimeUs)
        case let .status(state, thermal, message):
            type = .status
            payload.u8(state.rawValue)
            payload.u8(thermal)
            payload.str(message)
        case let .cameraSettings(c):
            type = .cameraSettings
            payload.u8(c.lens.rawValue)
            payload.f32(c.zoom)
            payload.bool(c.manualExposure)
            payload.f32(c.iso)
            payload.u32(c.shutterUs)
            payload.f32(c.exposureBias)
            payload.bool(c.manualWhiteBalance)
            payload.f32(c.temperatureK)
            payload.f32(c.tint)
            payload.bool(c.manualFocus)
            payload.f32(c.lensPosition)
            payload.u8(c.blur)
            payload.u8(c.rotation.rawValue)
            payload.bool(c.faceFocus)
            payload.u8(c.backgroundMode.rawValue)
            payload.u32(c.backgroundColor)
            payload.bool(c.mirrorHorizontal)
            payload.bool(c.mirrorVertical)
            payload.f32(c.panX)
            payload.f32(c.panY)
        case let .cameraState(c):
            type = .cameraState
            payload.u8(c.lenses)
            payload.u8(c.lens.rawValue)
            payload.f32(c.isoMin)
            payload.f32(c.isoMax)
            payload.u32(c.shutterMinUs)
            payload.u32(c.shutterMaxUs)
            payload.f32(c.biasMin)
            payload.f32(c.biasMax)
            payload.f32(c.zoomMax)
            payload.bool(c.manualFocusSupported)
            payload.f32(c.iso)
            payload.u32(c.shutterUs)
            payload.f32(c.temperatureK)
            payload.f32(c.tint)
            payload.f32(c.lensPosition)
            payload.bool(c.reactionsSupported)
            payload.bool(c.reactionsEnabled)
            payload.bool(c.gesturesEnabled)
            payload.bool(c.flipped)
        case let .backgroundImage(jpeg):
            type = .backgroundImage
            payload.bytes.append(contentsOf: jpeg)
        }
        var frame = ByteWriter()
        frame.u8(type.rawValue)
        frame.u32(UInt32(payload.bytes.count))
        frame.bytes.append(contentsOf: payload.bytes)
        return Data(frame.bytes)
    }
}

struct ByteWriter {
    var bytes: [UInt8] = []

    mutating func u8(_ v: UInt8) { bytes.append(v) }
    mutating func u16(_ v: UInt16) { le(UInt64(v), 2) }
    mutating func u32(_ v: UInt32) { le(UInt64(v), 4) }
    mutating func u64(_ v: UInt64) { le(v, 8) }
    mutating func f32(_ v: Float) { u32(v.bitPattern) }
    mutating func bool(_ v: Bool) { u8(v ? 1 : 0) }
    mutating func str(_ s: String) {
        let utf8 = Array(s.utf8)
        u16(UInt16(utf8.count))
        bytes.append(contentsOf: utf8)
    }

    private mutating func le(_ v: UInt64, _ n: Int) {
        for i in 0..<n { bytes.append(UInt8(truncatingIfNeeded: v >> (8 * UInt64(i)))) }
    }
}
