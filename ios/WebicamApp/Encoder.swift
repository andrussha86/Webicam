import Foundation
import VideoToolbox
import WebicamCore

enum EncoderError: Error {
    case createFailed(OSStatus)
}

/// Low-latency H.264 (VideoToolbox) → Annex B. Drops a frame instead of queueing
/// when the previous one is still being encoded.
final class Encoder {
    var onEncoded: ((Data, Bool, UInt64) -> Void)?

    private var session: VTCompressionSession?
    private let lock = NSLock()
    private var busy = false
    private var forceKey = true

    func configure(width: Int, height: Int, fps: Int, bitrateKbps: Int) throws {
        invalidate()
        let spec = [kVTVideoEncoderSpecification_EnableLowLatencyRateControl: kCFBooleanTrue] as CFDictionary
        var created: VTCompressionSession?
        let status = VTCompressionSessionCreate(
            allocator: nil, width: Int32(width), height: Int32(height), codecType: kCMVideoCodecType_H264,
            encoderSpecification: spec, imageBufferAttributes: nil, compressedDataAllocator: nil,
            outputCallback: nil, refcon: nil, compressionSessionOut: &created)
        guard status == noErr, let created else { throw EncoderError.createFailed(status) }
        let properties: [CFString: Any] = [
            kVTCompressionPropertyKey_RealTime: kCFBooleanTrue as Any,
            kVTCompressionPropertyKey_ProfileLevel: kVTProfileLevel_H264_ConstrainedHigh_AutoLevel,
            kVTCompressionPropertyKey_AllowFrameReordering: kCFBooleanFalse as Any,
            kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration: 2,
            kVTCompressionPropertyKey_AverageBitRate: bitrateKbps * 1000,
            kVTCompressionPropertyKey_ExpectedFrameRate: fps,
        ]
        VTSessionSetProperties(created, propertyDictionary: properties as CFDictionary)
        VTCompressionSessionPrepareToEncodeFrames(created)
        lock.withLock {
            session = created
            busy = false
            forceKey = true
        }
    }

    func invalidate() {
        let old: VTCompressionSession? = lock.withLock {
            defer { session = nil }
            return session
        }
        if let old { VTCompressionSessionInvalidate(old) }
    }

    func forceKeyframe() { lock.withLock { forceKey = true } }

    func encode(_ pixelBuffer: CVPixelBuffer, time: CMTime) {
        let state: (VTCompressionSession, Bool)? = lock.withLock {
            guard let current = self.session, !self.busy else { return nil }
            self.busy = true
            defer { self.forceKey = false }
            return (current, self.forceKey)
        }
        guard let state else { return }
        let (session, key) = state
        let options = key ? [kVTEncodeFrameOptionKey_ForceKeyFrame: true] as CFDictionary : nil
        let captureUs = microseconds(time)
        let status = VTCompressionSessionEncodeFrame(
            session, imageBuffer: pixelBuffer, presentationTimeStamp: time, duration: .invalid,
            frameProperties: options, infoFlagsOut: nil
        ) { [weak self] status, _, sample in
            guard let self else { return }
            self.lock.withLock { self.busy = false }
            guard status == noErr, let sample else { return }
            self.emit(sample, captureUs: captureUs)
        }
        if status != noErr { lock.withLock { busy = false } }
    }

    private func emit(_ sample: CMSampleBuffer, captureUs: UInt64) {
        guard let block = CMSampleBufferGetDataBuffer(sample),
              let format = CMSampleBufferGetFormatDescription(sample) else { return }
        var keyframe = true
        if let attachments = CMSampleBufferGetSampleAttachmentsArray(sample, createIfNecessary: false)
            as? [[CFString: Any]], let first = attachments.first {
            keyframe = !((first[kCMSampleAttachmentKey_NotSync] as? Bool) ?? false)
        }
        var length = 0
        var pointer: UnsafeMutablePointer<CChar>?
        guard CMBlockBufferGetDataPointer(block, atOffset: 0, lengthAtOffsetOut: nil, totalLengthOut: &length,
                                          dataPointerOut: &pointer) == noErr, let pointer else { return }
        let avcc = Data(bytes: pointer, count: length)

        var nalLength: Int32 = 4
        var parameterSets: [Data]?
        if keyframe {
            var sets: [Data] = []
            var count = 0
            CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
                format, parameterSetIndex: 0, parameterSetPointerOut: nil, parameterSetSizeOut: nil,
                parameterSetCountOut: &count, nalUnitHeaderLengthOut: &nalLength)
            for index in 0..<count {
                var setPointer: UnsafePointer<UInt8>?
                var setSize = 0
                if CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
                    format, parameterSetIndex: index, parameterSetPointerOut: &setPointer,
                    parameterSetSizeOut: &setSize, parameterSetCountOut: nil, nalUnitHeaderLengthOut: nil) == noErr,
                    let setPointer {
                    sets.append(Data(bytes: setPointer, count: setSize))
                }
            }
            parameterSets = sets
        }
        let annexB = AnnexB.fromAVCC(avcc, nalLengthSize: Int(nalLength), parameterSets: parameterSets)
        onEncoded?(annexB, keyframe, captureUs)
    }
}
