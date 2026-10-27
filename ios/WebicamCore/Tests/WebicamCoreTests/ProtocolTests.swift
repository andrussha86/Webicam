import XCTest
@testable import WebicamCore

/// Expected values of shared/testvectors/*.bin (mirrors windows/core/include/webicam/testvectors.h).
private let vectors: [(String, Message)] = [
    ("hello_pc.bin", .hello(version: 3, model: nil, formats: [])),
    ("hello_phone.bin", .hello(version: 3, model: "iPhone16,1", formats: [
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
    ("camera_settings.bin", .cameraSettings({
        var c = CameraSettings()
        c.lens = .telephoto
        c.zoom = 1.5
        c.manualExposure = true
        c.iso = 400
        c.shutterUs = 8000
        c.exposureBias = -0.5
        c.manualWhiteBalance = true
        c.temperatureK = 4500
        c.tint = 10
        c.manualFocus = true
        c.lensPosition = 0.25
        c.blur = 60
        c.rotate180 = true
        c.faceFocus = true
        c.backgroundMode = .image
        c.backgroundColor = 0x336699
        return c
    }())),
    ("camera_state.bin", .cameraState({
        var c = CameraState()
        c.lenses = 0x1F
        c.lens = .wide
        c.isoMin = 50
        c.isoMax = 3200
        c.shutterMinUs = 20
        c.shutterMaxUs = 16666
        c.biasMin = -8
        c.biasMax = 8
        c.zoomMax = 10
        c.manualFocusSupported = true
        c.iso = 200
        c.shutterUs = 10000
        c.temperatureK = 5200
        c.tint = -5
        c.lensPosition = 0.75
        c.reactionsSupported = true
        c.reactionsEnabled = true
        return c
    }())),
    ("background_image.bin", .backgroundImage(Data([0xFF, 0xD8, 0xFF]))),
    ("background_image_empty.bin", .backgroundImage(Data())),
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
        parser.feed(Data([0x0B, 0, 0, 0, 0]))
        XCTAssertThrowsError(try parser.next())
    }

    /// camera_settings.bin with payload bytes replaced; v2 fields take 34 bytes, then
    /// face_focus (34), background_mode (35), background_color (36...39).
    private func settingsWith(_ offset: Int, _ patch: [UInt8]) throws -> Data {
        var bytes = [UInt8](try Data(contentsOf: vectorsDir.appendingPathComponent("camera_settings.bin")))
        bytes.replaceSubrange((5 + offset)..<(5 + offset + patch.count), with: patch)
        return Data(bytes)
    }

    func testRejectsBackgroundModeFour() throws {
        var parser = MessageParser()
        parser.feed(try settingsWith(35, [4]))
        XCTAssertThrowsError(try parser.next())
    }

    func testRejectsColorHighByte() throws {
        var parser = MessageParser()
        parser.feed(try settingsWith(36, [0, 0, 0, 1]))
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
