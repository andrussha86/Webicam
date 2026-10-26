import Foundation

/// Webicam wire protocol v1 — see shared/protocol.md.
public enum ProtocolConstants {
    public static let version: UInt16 = 1
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

enum MessageType: UInt8 {
    case hello = 1, configure, videoFrame, requestKeyframe, ping, pong, status
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
    mutating func str(_ s: String) {
        let utf8 = Array(s.utf8)
        u16(UInt16(utf8.count))
        bytes.append(contentsOf: utf8)
    }

    private mutating func le(_ v: UInt64, _ n: Int) {
        for i in 0..<n { bytes.append(UInt8(truncatingIfNeeded: v >> (8 * UInt64(i)))) }
    }
}
