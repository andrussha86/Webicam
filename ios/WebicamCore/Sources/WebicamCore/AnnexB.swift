import Foundation

public enum AnnexB {
    private static let startCode: [UInt8] = [0, 0, 0, 1]

    /// Converts length-prefixed (AVCC) NAL units to Annex B. Parameter sets (SPS, PPS),
    /// when given, are prepended — pass them for keyframes.
    public static func fromAVCC(_ avcc: Data, nalLengthSize: Int = 4, parameterSets: [Data]? = nil) -> Data {
        var out: [UInt8] = []
        out.reserveCapacity(avcc.count + 64)
        for set in parameterSets ?? [] {
            out.append(contentsOf: startCode)
            out.append(contentsOf: set)
        }
        let bytes = [UInt8](avcc)
        var pos = 0
        while pos + nalLengthSize <= bytes.count {
            var length = 0
            for i in 0..<nalLengthSize { length = length << 8 | Int(bytes[pos + i]) }
            pos += nalLengthSize
            guard length > 0, pos + length <= bytes.count else { break }
            out.append(contentsOf: startCode)
            out.append(contentsOf: bytes[pos..<(pos + length)])
            pos += length
        }
        return Data(out)
    }
}

/// Default bitrate table (kbit/s) shared with the PC side.
public func defaultBitrateKbps(height: UInt16, fps: UInt8) -> UInt32 {
    switch height {
    case 720: return fps > 30 ? 12000 : 8000
    case 1080: return fps > 30 ? 25000 : 16000
    case 2160: return 45000
    default: return 16000
    }
}
