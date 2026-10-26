import XCTest
@testable import WebicamCore

/// Expected values of shared/testvectors/*.bin (mirrors windows/core/include/webicam/testvectors.h).
private let vectors: [(String, Message)] = [
    ("hello_pc.bin", .hello(version: 1, model: nil, formats: [])),
    ("hello_phone.bin", .hello(version: 1, model: "iPhone16,1", formats: [
        CaptureFormat(width: 1280, height: 720, maxFps: 60),
        CaptureFormat(width: 1920, height: 1080, maxFps: 60),
        CaptureFormat(width: 3840, height: 2160, maxFps: 30),
    ])),
    ("configure_1080p60.bin", .configure(width: 1920, height: 1080, fps: 60, bitrateKbps: 25000)),
    ("video_frame_key.bin", .videoFrame(captureTimeUs: 123_456_789, keyframe: true,
                                        annexB: Data([0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1F]))),
    ("request_keyframe.bin", .requestKeyframe),
    ("ping.bin", .ping(pcTimeUs: 1000)),
    ("pong.bin", .pong(pcTimeUs: 1000, phoneTimeUs: 2000)),
    ("status_thermal.bin", .status(state: .thermalThrottled, thermal: 2, message: "serious")),
]

private let vectorsDir: URL = {
    var url = URL(fileURLWithPath: #filePath)
    for _ in 0..<5 { url.deleteLastPathComponent() }  // → repository root
    return url.appendingPathComponent("shared/testvectors")
}()

final class ProtocolTests: XCTestCase {
    func testVectorsMatchFiles() throws {
        for (file, message) in vectors {
            let bytes = try Data(contentsOf: vectorsDir.appendingPathComponent(file))
            XCTAssertEqual(message.encoded(), bytes, file)
            var parser = MessageParser()
            parser.feed(bytes)
            XCTAssertEqual(try parser.next(), message, file)
            XCTAssertNil(try parser.next(), file)
        }
    }

    func testByteByByteFeed() throws {
        var stream = Data()
        for (_, message) in vectors { stream.append(message.encoded()) }
        var parser = MessageParser()
        var parsed: [Message] = []
        for byte in stream {
            parser.feed(Data([byte]))
            while let message = try parser.next() { parsed.append(message) }
        }
        XCTAssertEqual(parsed, vectors.map { $0.1 })
    }

    func testOversizeLengthThrows() {
        var parser = MessageParser()
        parser.feed(Data([0x03, 0x01, 0x00, 0x00, 0x01]))  // 16 777 217
        XCTAssertThrowsError(try parser.next())
        XCTAssertThrowsError(try parser.next())
    }

    func testUnknownTypeThrows() {
        var parser = MessageParser()
        parser.feed(Data([0x09, 0, 0, 0, 0]))
        XCTAssertThrowsError(try parser.next())
    }

    func testDefaultBitrate() {
        XCTAssertEqual(defaultBitrateKbps(height: 1080, fps: 60), 25000)
        XCTAssertEqual(defaultBitrateKbps(height: 2160, fps: 30), 45000)
        XCTAssertEqual(defaultBitrateKbps(height: 720, fps: 30), 8000)
    }
}

final class AnnexBTests: XCTestCase {
    func testConvertsAndPrependsParameterSets() {
        let avcc = Data([0x00, 0x00, 0x00, 0x02, 0xAA, 0xBB, 0x00, 0x00, 0x00, 0x01, 0xCC])
        let out = AnnexB.fromAVCC(avcc, parameterSets: [Data([0x67, 0x42]), Data([0x68, 0xCE])])
        XCTAssertEqual(out, Data([0, 0, 0, 1, 0x67, 0x42, 0, 0, 0, 1, 0x68, 0xCE,
                                  0, 0, 0, 1, 0xAA, 0xBB, 0, 0, 0, 1, 0xCC]))
    }

    func testTruncatedNalIsDropped() {
        let avcc = Data([0x00, 0x00, 0x00, 0x05, 0xAA])
        XCTAssertEqual(AnnexB.fromAVCC(avcc), Data())
    }
}
