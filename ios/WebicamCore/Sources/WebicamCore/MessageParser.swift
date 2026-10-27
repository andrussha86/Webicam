import Foundation

public enum ProtocolError: Error, Equatable {
    case unknownType(UInt8)
    case payloadTooLarge(UInt32)
    case malformed(UInt8)
}

/// Incremental parser for the Webicam framing. After an error it keeps throwing.
public struct MessageParser {
    private var buffer: [UInt8] = []
    private var offset = 0
    private var failure: ProtocolError?

    public init() {}

    public var buffered: Int { buffer.count - offset }

    public mutating func feed(_ data: Data) {
        if offset > 0 && offset == buffer.count {
            buffer.removeAll(keepingCapacity: true)
            offset = 0
        }
        buffer.append(contentsOf: data)
    }

    /// Returns the next complete message, nil if more bytes are needed.
    public mutating func next() throws -> Message? {
        if let failure { throw failure }
        guard buffered >= 5 else { return nil }
        let type = buffer[offset]
        let length = UInt32(buffer[offset + 1]) | UInt32(buffer[offset + 2]) << 8
            | UInt32(buffer[offset + 3]) << 16 | UInt32(buffer[offset + 4]) << 24
        guard let messageType = MessageType(rawValue: type) else {
            return try fail(.unknownType(type))
        }
        guard length <= UInt32(ProtocolConstants.maxPayloadBytes) else {
            return try fail(.payloadTooLarge(length))
        }
        guard buffered >= 5 + Int(length) else { return nil }
        let start = offset + 5
        let payload = buffer[start..<(start + Int(length))]
        guard let message = Self.decode(messageType, ByteReader(payload)) else {
            return try fail(.malformed(type))
        }
        offset = start + Int(length)
        if offset > 1 << 20 && offset * 2 > buffer.count {
            buffer.removeFirst(offset)
            offset = 0
        }
        return message
    }

    private mutating func fail(_ error: ProtocolError) throws -> Message? {
        failure = error
        throw error
    }

    private static func decode(_ type: MessageType, _ reader: ByteReader) -> Message? {
        var r = reader
        switch type {
        case .hello:
            guard let version = r.u16() else { return nil }
            if r.remaining == 0 { return .hello(version: version, model: nil, formats: []) }
            guard let model = r.str(), let count = r.u16() else { return nil }
            var formats: [CaptureFormat] = []
            for _ in 0..<count {
                guard let w = r.u16(), let h = r.u16(), let fps = r.u8() else { return nil }
                formats.append(CaptureFormat(width: w, height: h, maxFps: fps))
            }
            return r.remaining == 0 ? .hello(version: version, model: model, formats: formats) : nil
        case .configure:
            guard let w = r.u16(), let h = r.u16(), let fps = r.u8(), let bitrate = r.u32(), r.remaining == 0
            else { return nil }
            return .configure(width: w, height: h, fps: fps, bitrateKbps: bitrate)
        case .videoFrame:
            guard let time = r.u64(), let flags = r.u8() else { return nil }
            return .videoFrame(captureTimeUs: time, keyframe: flags & 1 != 0, annexB: r.rest())
        case .requestKeyframe:
            return r.remaining == 0 ? .requestKeyframe : nil
        case .ping:
            guard let pc = r.u64(), r.remaining == 0 else { return nil }
            return .ping(pcTimeUs: pc)
        case .pong:
            guard let pc = r.u64(), let phone = r.u64(), r.remaining == 0 else { return nil }
            return .pong(pcTimeUs: pc, phoneTimeUs: phone)
        case .status:
            guard let raw = r.u8(), let state = StreamState(rawValue: raw), let thermal = r.u8(),
                  let message = r.str(), r.remaining == 0
            else { return nil }
            return .status(state: state, thermal: thermal, message: message)
        case .cameraSettings:
            var c = CameraSettings()
            guard let rawLens = r.u8(), let lens = Lens(rawValue: rawLens), let zoom = r.f32(),
                  let manualExposure = r.bool(), let iso = r.f32(), let shutter = r.u32(), let bias = r.f32(),
                  let manualWb = r.bool(), let temperature = r.f32(), let tint = r.f32(),
                  let manualFocus = r.bool(), let lensPosition = r.f32(), let blur = r.u8(),
                  let rawRotation = r.u8(), let rotation = Rotation(rawValue: rawRotation),
                  let face = r.bool(), let rawMode = r.u8(),
                  let mode = BackgroundMode(rawValue: rawMode), let color = r.u32(), color <= 0xFFFFFF,
                  let mirrorH = r.bool(), let mirrorV = r.bool(), let panX = r.f32(), let panY = r.f32(),
                  r.remaining == 0
            else { return nil }
            c.lens = lens
            c.zoom = zoom
            c.manualExposure = manualExposure
            c.iso = iso
            c.shutterUs = shutter
            c.exposureBias = bias
            c.manualWhiteBalance = manualWb
            c.temperatureK = temperature
            c.tint = tint
            c.manualFocus = manualFocus
            c.lensPosition = lensPosition
            c.blur = blur
            c.rotation = rotation
            c.faceFocus = face
            c.backgroundMode = mode
            c.backgroundColor = color
            c.mirrorHorizontal = mirrorH
            c.mirrorVertical = mirrorV
            c.panX = panX
            c.panY = panY
            return .cameraSettings(c)
        case .cameraState:
            var c = CameraState()
            guard let lenses = r.u8(), let rawLens = r.u8(), let lens = Lens(rawValue: rawLens),
                  let isoMin = r.f32(), let isoMax = r.f32(), let shutterMin = r.u32(), let shutterMax = r.u32(),
                  let biasMin = r.f32(), let biasMax = r.f32(), let zoomMax = r.f32(), let focus = r.bool(),
                  let iso = r.f32(), let shutter = r.u32(), let temperature = r.f32(), let tint = r.f32(),
                  let lensPosition = r.f32(), let reactionsSupported = r.bool(), let reactionsEnabled = r.bool(),
                  let gestures = r.bool(), let flipped = r.bool(), r.remaining == 0
            else { return nil }
            c.lenses = lenses
            c.lens = lens
            c.isoMin = isoMin
            c.isoMax = isoMax
            c.shutterMinUs = shutterMin
            c.shutterMaxUs = shutterMax
            c.biasMin = biasMin
            c.biasMax = biasMax
            c.zoomMax = zoomMax
            c.manualFocusSupported = focus
            c.iso = iso
            c.shutterUs = shutter
            c.temperatureK = temperature
            c.tint = tint
            c.lensPosition = lensPosition
            c.reactionsSupported = reactionsSupported
            c.reactionsEnabled = reactionsEnabled
            c.gesturesEnabled = gestures
            c.flipped = flipped
            return .cameraState(c)
        case .backgroundImage:
            return .backgroundImage(r.rest())
        case .pair:
            guard let code = r.str(), r.remaining == 0 else { return nil }
            return .pair(code: code)
        case .telemetry:
            guard let battery = r.u8(), battery <= 100 || battery == 255, let rawCharging = r.u8(),
                  let charging = Charging(rawValue: rawCharging), let thermal = r.u8(), thermal <= 3,
                  r.remaining == 0
            else { return nil }
            return .telemetry(battery: battery, charging: charging, thermal: thermal)
        case .micSettings:
            guard let enabled = r.bool(), r.remaining == 0 else { return nil }
            return .micSettings(enabled: enabled)
        case .audioFrame:
            guard let time = r.u64(), r.remaining % 2 == 0 else { return nil }
            var samples: [Int16] = []
            samples.reserveCapacity(r.remaining / 2)
            while r.remaining > 0, let v = r.u16() { samples.append(Int16(bitPattern: v)) }
            return .audioFrame(captureTimeUs: time, samples: samples)
        }
    }
}

struct ByteReader {
    private let bytes: ArraySlice<UInt8>
    private var pos: Int

    init(_ bytes: ArraySlice<UInt8>) {
        self.bytes = bytes
        pos = bytes.startIndex
    }

    var remaining: Int { bytes.endIndex - pos }

    mutating func u8() -> UInt8? { le(1).map { UInt8($0) } }
    mutating func u16() -> UInt16? { le(2).map { UInt16($0) } }
    mutating func u32() -> UInt32? { le(4).map { UInt32($0) } }
    mutating func u64() -> UInt64? { le(8) }
    mutating func f32() -> Float? { u32().map { Float(bitPattern: $0) } }
    mutating func bool() -> Bool? {
        guard let v = u8(), v <= 1 else { return nil }
        return v == 1
    }

    mutating func str() -> String? {
        guard let length = u16(), remaining >= Int(length) else { return nil }
        let s = String(decoding: bytes[pos..<(pos + Int(length))], as: UTF8.self)
        pos += Int(length)
        return s
    }

    mutating func rest() -> Data {
        let data = Data(bytes[pos..<bytes.endIndex])
        pos = bytes.endIndex
        return data
    }

    private mutating func le(_ n: Int) -> UInt64? {
        guard remaining >= n else { return nil }
        var v: UInt64 = 0
        for i in 0..<n { v |= UInt64(bytes[pos + i]) << (8 * UInt64(i)) }
        pos += n
        return v
    }
}
