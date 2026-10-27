import Foundation
import Observation
import WebicamCore

/// What the status screen shows.
@Observable
final class AppModel {
    var connected = false
    var stopped = false
    var format = "—"
    var bitrate = "—"
    var warning: String?
}

/// Wires capture → encoder → server and applies the PC's commands.
final class ControlHandler {
    let model = AppModel()

    private let capture = CaptureEngine()
    private let encoder = Encoder()
    private let server = StreamServer()
    private var active: (width: Int, height: Int, fps: Int, bitrateKbps: Int)?
    private var throttled = false
    private var stopped = false  // logic copy of model.stopped, safe off the main thread
    private let stateTimer = DispatchSource.makeTimerSource(queue: DispatchQueue(label: "webicam.camera-state"))

    init() {
        capture.onFrame = { [encoder] pixelBuffer, time in encoder.encode(pixelBuffer, time: time) }
        encoder.onEncoded = { [server] annexB, keyframe, captureUs in
            server.sendVideo(.videoFrame(captureTimeUs: captureUs, keyframe: keyframe, annexB: annexB),
                             keyframe: keyframe)
        }
        server.onFrameDropped = { [encoder] in encoder.forceKeyframe() }
        server.onConnectionChange = { [weak self] connected in self?.connectionChanged(connected) }
        server.onMessage = { [weak self] message in self?.handle(message) }
        NotificationCenter.default.addObserver(
            forName: ProcessInfo.thermalStateDidChangeNotification, object: nil, queue: nil
        ) { [weak self] _ in self?.thermalChanged() }
        // Current ISO/shutter/white balance for the PC's sliders while they are on auto.
        stateTimer.schedule(deadline: .now() + 0.5, repeating: 0.5)
        stateTimer.setEventHandler { [weak self] in self?.sendCameraState() }
        stateTimer.resume()
    }

    func start() {
        server.onListenError = { [weak self] error in
            self?.updateModel { $0.warning = "Не удалось открыть порт: \(error.localizedDescription)" }
        }
        server.start()
    }

    /// The «Стоп»/«Старт» button.
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

    private func connectionChanged(_ connected: Bool) {
        updateModel { $0.connected = connected }
        if connected {
            server.send(.hello(version: ProtocolConstants.version, model: Self.machineModel(),
                               formats: capture.supportedFormats))
        } else {
            capture.stop()
        }
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

    private static func machineModel() -> String {
        var info = utsname()
        uname(&info)
        return withUnsafeBytes(of: &info.machine) { raw in
            String(decoding: raw.prefix(while: { $0 != 0 }), as: UTF8.self)
        }
    }
}
