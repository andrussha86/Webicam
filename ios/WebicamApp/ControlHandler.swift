import Foundation
import Observation
import UIKit
import WebicamCore

/// What the status screen shows.
@Observable
final class AppModel {
    var connected = false
    /// How the PC is connected; nil while waiting.
    var link: PhoneLink?
    /// What the PC asks for when it connects over Wi-Fi.
    var pairingCode = ""
    /// The phone cannot be found on the network (no local network permission).
    var wifiUnavailable = false
    /// The microphone is being sent to the PC.
    var micActive = false
    var stopped = false
    var format = "—"
    var bitrate = "—"
    var warning: String?
    /// Why the PC cannot reach the phone; shown instead of `warning` while set.
    var listenError: String?
}

/// Wires capture → encoder → server and applies the PC's commands.
final class ControlHandler {
    let model = AppModel()

    private let capture = CaptureEngine()
    private let encoder = Encoder()
    private let server: StreamServer
    private let mic = MicCapture()
    private var active: (width: Int, height: Int, fps: Int, bitrateKbps: Int)?
    private var throttled = false
    private var stopped = false  // logic copy of model.stopped, safe off the main thread
    private let stateTimer = DispatchSource.makeTimerSource(queue: DispatchQueue(label: "webicam.camera-state"))
    private var telemetryTimer: Timer?

    init() {
        let code = Self.storedPairingCode()
        server = StreamServer(phoneId: Self.phoneId(), pairingCode: code)
        model.pairingCode = code
        capture.onFrame = { [encoder] pixelBuffer, time in encoder.encode(pixelBuffer, time: time) }
        encoder.onEncoded = { [server] annexB, keyframe, captureUs in
            server.sendVideo(.videoFrame(captureTimeUs: captureUs, keyframe: keyframe, annexB: annexB),
                             keyframe: keyframe)
        }
        server.onFrameDropped = { [encoder] in encoder.forceKeyframe() }
        mic.onChunk = { [server] samples, captureUs in
            server.sendAudio(.audioFrame(captureTimeUs: captureUs, samples: samples))
        }
        mic.onRunningChange = { [weak self] running in self?.updateModel { $0.micActive = running } }
        mic.onError = { [weak self] text in
            guard let self else { return }
            self.server.send(.status(state: .error, thermal: self.thermalLevel(), message: text))
        }
        server.onConnectionChange = { [weak self] link in self?.connectionChanged(link) }
        server.onMessage = { [weak self] message in self?.handle(message) }
        capture.onBackgroundError = { [weak self] in
            guard let self else { return }
            self.server.send(.status(state: .error, thermal: self.thermalLevel(),
                                     message: "Не удалось открыть картинку фона"))
        }
        NotificationCenter.default.addObserver(
            forName: ProcessInfo.thermalStateDidChangeNotification, object: nil, queue: nil
        ) { [weak self] _ in self?.thermalChanged() }
        // Current ISO/shutter/white balance for the PC's sliders while they are on auto.
        stateTimer.schedule(deadline: .now() + 0.5, repeating: 0.5)
        stateTimer.setEventHandler { [weak self] in self?.sendCameraState() }
        stateTimer.resume()
    }

    func start() {
        server.onListenStateChange = { [weak self] error in
            let text = error.map { "Не удалось открыть порт: \($0.localizedDescription)" }
            self?.updateModel { $0.listenError = text }
        }
        server.onWifiUnavailable = { [weak self] in self?.updateModel { $0.wifiUnavailable = true } }
        server.start()
    }

    /// The «Пауза»/«Возобновить» button.
    func toggleStopped() {
        stopped.toggle()
        let stopping = stopped
        updateModel { $0.stopped = stopping }
        if stopping {
            capture.stop()
            server.send(.status(state: .stoppedByUser, thermal: thermalLevel(), message: ""))
        } else if active != nil {
            encoder.forceKeyframe()
            capture.start()
            server.send(.status(state: .streaming, thermal: thermalLevel(), message: ""))
        }
    }

    /// A new six-digit code; PCs that knew the old one have to be told the new one.
    func regeneratePairingCode() {
        let code = Self.newPairingCode()
        UserDefaults.standard.set(code, forKey: Self.pairingCodeKey)
        server.setPairingCode(code)
        updateModel { $0.pairingCode = code }
    }

    private func connectionChanged(_ link: PhoneLink?) {
        updateModel {
            $0.connected = link != nil
            $0.link = link
        }
        if link != nil {
            server.send(.hello(version: ProtocolConstants.version, model: Self.machineModel(),
                               formats: capture.supportedFormats))
            DispatchQueue.main.async { [weak self] in self?.startTelemetry() }
        } else {
            capture.stop()
            mic.setEnabled(false)
            DispatchQueue.main.async { [weak self] in self?.stopTelemetry() }
        }
    }

    // MARK: Telemetry (main thread: UIDevice)

    private func startTelemetry() {
        UIDevice.current.isBatteryMonitoringEnabled = true
        sendTelemetry()
        telemetryTimer?.invalidate()
        telemetryTimer = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { [weak self] _ in
            self?.sendTelemetry()
        }
    }

    private func stopTelemetry() {
        telemetryTimer?.invalidate()
        telemetryTimer = nil
    }

    private func sendTelemetry() {
        let device = UIDevice.current
        let level = device.batteryLevel
        let battery: UInt8 = level < 0 ? 255 : UInt8((level * 100).rounded())
        let charging: Charging
        switch device.batteryState {
        case .unplugged: charging = .unplugged
        case .charging: charging = .charging
        case .full: charging = .full
        default: charging = .unknown
        }
        server.send(.telemetry(battery: battery, charging: charging, thermal: thermalLevel()))
    }

    private func handle(_ message: Message) {
        switch message {
        case let .configure(width, height, fps, bitrateKbps):
            apply(width: Int(width), height: Int(height), fps: Int(fps), bitrateKbps: Int(bitrateKbps))
        case let .cameraSettings(settings):
            do {
                try capture.apply(settings)
            } catch {
                server.send(.status(state: .error, thermal: thermalLevel(),
                                    message: "Объектив недоступен в этом формате: \(error)"))
            }
            sendCameraState()
        case .requestKeyframe:
            encoder.forceKeyframe()
        case let .ping(pcTimeUs):
            server.send(.pong(pcTimeUs: pcTimeUs, phoneTimeUs: hostTimeUs()))
        case let .backgroundImage(jpeg):
            capture.setBackgroundImage(jpeg)
        case let .micSettings(enabled):
            mic.setEnabled(enabled)
        default:
            break
        }
    }

    private func apply(width: Int, height: Int, fps: Int, bitrateKbps: Int) {
        active = (width, height, fps, bitrateKbps)
        var effectiveFps = fps
        var effectiveBitrate = bitrateKbps
        if throttled {
            effectiveFps = min(fps, 30)
            effectiveBitrate = bitrateKbps * 7 / 10
        }
        do {
            effectiveFps = try capture.configure(width: width, height: height, fps: effectiveFps)
            try encoder.configure(width: width, height: height, fps: effectiveFps, bitrateKbps: effectiveBitrate)
        } catch {
            server.send(.status(state: .error, thermal: thermalLevel(), message: "Формат не поддерживается: \(error)"))
            return
        }
        updateModel {
            $0.format = "\(width)×\(height) @ \(effectiveFps)"
            $0.bitrate = "\(effectiveBitrate / 1000) Мбит/с"
        }
        if !stopped { capture.start() }
        // The PC may have missed earlier status changes; always tell it where we are.
        let state: StreamState = stopped ? .stoppedByUser : (throttled ? .thermalThrottled : .streaming)
        server.send(.status(state: state, thermal: thermalLevel(), message: ""))
        sendCameraState()
    }

    private func sendCameraState() {
        if let state = capture.state() { server.send(.cameraState(state)) }
    }

    private func thermalChanged() {
        let state = ProcessInfo.processInfo.thermalState
        switch state {
        case .serious:
            throttled = true
            updateModel { $0.warning = "Телефон нагрелся: качество снижено" }
            server.send(.status(state: .thermalThrottled, thermal: 2, message: "serious"))
        case .critical:
            throttled = true
            updateModel { $0.warning = "Телефон перегрелся" }
            server.send(.status(state: .error, thermal: 3, message: "critical"))
        default:
            throttled = false
            updateModel { $0.warning = nil }
            server.send(.status(state: .streaming, thermal: thermalLevel(), message: ""))
        }
        if let active { apply(width: active.width, height: active.height, fps: active.fps, bitrateKbps: active.bitrateKbps) }
    }

    private func thermalLevel() -> UInt8 {
        switch ProcessInfo.processInfo.thermalState {
        case .nominal: return 0
        case .fair: return 1
        case .serious: return 2
        case .critical: return 3
        @unknown default: return 0
        }
    }

    private func updateModel(_ change: @escaping (AppModel) -> Void) {
        DispatchQueue.main.async { [model] in change(model) }
    }

    private static let pairingCodeKey = "webicam.pairingCode"

    private static func storedPairingCode() -> String {
        if let code = UserDefaults.standard.string(forKey: pairingCodeKey), code.count == 6 { return code }
        let code = newPairingCode()
        UserDefaults.standard.set(code, forKey: pairingCodeKey)
        return code
    }

    private static func newPairingCode() -> String {
        String(format: "%06d", Int.random(in: 0...999_999))
    }

    /// Lets the PC recognise this phone (and its code) when its Wi-Fi address changes.
    private static func phoneId() -> String {
        let key = "webicam.phoneId"
        if let id = UserDefaults.standard.string(forKey: key) { return id }
        let id = UUID().uuidString
        UserDefaults.standard.set(id, forKey: key)
        return id
    }

    private static func machineModel() -> String {
        var info = utsname()
        uname(&info)
        return withUnsafeBytes(of: &info.machine) { raw in
            String(decoding: raw.prefix(while: { $0 != 0 }), as: UTF8.self)
        }
    }
}
