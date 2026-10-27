import AVFoundation
import WebicamCore

/// The phone's microphone as 48 kHz mono Int16 chunks of 20 ms with host-clock timestamps.
/// Runs only while the PC asks for it; survives interruptions (calls, Siri) and route changes.
final class MicCapture {
    var onChunk: (([Int16], UInt64) -> Void)?
    /// Why the microphone cannot run; reported once per attempt.
    var onError: ((String) -> Void)?
    var onRunningChange: ((Bool) -> Void)?

    private static let chunkSamples = ProtocolConstants.audioSampleRate / 50

    private let queue = DispatchQueue(label: "webicam.mic")
    private let engine = AVAudioEngine()
    private let outputFormat = AVAudioFormat(commonFormat: .pcmFormatInt16,
                                             sampleRate: Double(ProtocolConstants.audioSampleRate),
                                             channels: 1, interleaved: true)!
    private var converter: AVAudioConverter?
    private var wanted = false
    private var running = false
    // Touched only from the tap's thread.
    private var pending: [Int16] = []
    private var pendingTimeUs: UInt64 = 0

    init() {
        let center = NotificationCenter.default
        center.addObserver(forName: AVAudioSession.interruptionNotification, object: nil, queue: nil) { [weak self] note in
            guard let raw = note.userInfo?[AVAudioSessionInterruptionTypeKey] as? UInt,
                  AVAudioSession.InterruptionType(rawValue: raw) == .ended else { return }
            self?.queue.async { self?.restart() }
        }
        center.addObserver(forName: .AVAudioEngineConfigurationChange, object: engine, queue: nil) { [weak self] _ in
            self?.queue.async { self?.restart() }
        }
    }

    func setEnabled(_ enabled: Bool) {
        queue.async { [weak self] in
            guard let self, self.wanted != enabled else { return }
            self.wanted = enabled
            if enabled { self.startIfAllowed() } else { self.stop() }
        }
    }

    private func restart() {
        guard wanted else { return }
        stop()
        startIfAllowed()
    }

    private func startIfAllowed() {
        switch AVAudioApplication.shared.recordPermission {
        case .granted:
            start()
        case .undetermined:
            AVAudioApplication.requestRecordPermission { [weak self] granted in
                self?.queue.async {
                    guard let self, self.wanted else { return }
                    if granted { self.start() } else { self.onError?(Self.deniedText) }
                }
            }
        default:
            onError?(Self.deniedText)
        }
    }

    private static let deniedText = "Нет доступа к микрофону: Настройки → Webicam → Микрофон"

    private func start() {
        guard !running else { return }
        do {
            let session = AVAudioSession.sharedInstance()
            try session.setCategory(.record, mode: .default)
            try session.setPreferredSampleRate(outputFormat.sampleRate)
            try session.setActive(true)
            let input = engine.inputNode
            let inputFormat = input.outputFormat(forBus: 0)
            guard inputFormat.sampleRate > 0, let converter = AVAudioConverter(from: inputFormat, to: outputFormat) else {
                onError?("Микрофон недоступен")
                return
            }
            self.converter = converter
            pending.removeAll(keepingCapacity: true)
            input.installTap(onBus: 0, bufferSize: 1024, format: inputFormat) { [weak self] buffer, time in
                self?.process(buffer, at: time)
            }
            engine.prepare()
            try engine.start()
            running = true
            onRunningChange?(true)
        } catch {
            engine.inputNode.removeTap(onBus: 0)
            onError?("Микрофон не запустился: \(error.localizedDescription)")
        }
    }

    private func stop() {
        guard running else { return }
        engine.inputNode.removeTap(onBus: 0)
        engine.stop()
        try? AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)
        running = false
        onRunningChange?(false)
    }

    /// Converts to 48 kHz mono Int16 and cuts the stream into 20 ms chunks.
    private func process(_ buffer: AVAudioPCMBuffer, at time: AVAudioTime) {
        guard let converter else { return }
        let ratio = outputFormat.sampleRate / buffer.format.sampleRate
        let capacity = AVAudioFrameCount(Double(buffer.frameLength) * ratio) + 32
        guard let converted = AVAudioPCMBuffer(pcmFormat: outputFormat, frameCapacity: capacity) else { return }
        var supplied = false
        var error: NSError?
        converter.convert(to: converted, error: &error) { _, status in
            if supplied {
                status.pointee = .noDataNow
                return nil
            }
            supplied = true
            status.pointee = .haveData
            return buffer
        }
        guard error == nil, let channel = converted.int16ChannelData, converted.frameLength > 0 else { return }

        if pending.isEmpty {
            pendingTimeUs = time.isHostTimeValid
                ? UInt64(AVAudioTime.seconds(forHostTime: time.hostTime) * 1_000_000)
                : hostTimeUs()
        }
        pending.append(contentsOf: UnsafeBufferPointer(start: channel[0], count: Int(converted.frameLength)))
        while pending.count >= Self.chunkSamples {
            onChunk?(Array(pending.prefix(Self.chunkSamples)), pendingTimeUs)
            pending.removeFirst(Self.chunkSamples)
            pendingTimeUs += 20_000
        }
    }
}
