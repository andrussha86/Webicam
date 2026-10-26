import Foundation
import Network
import WebicamCore

/// TCP server on 127.0.0.1:47800 — reachable only through the usbmux USB tunnel.
/// One client at a time; a new connection replaces the old one.
final class StreamServer {
    var onMessage: ((Message) -> Void)?
    var onConnectionChange: ((Bool) -> Void)?
    /// A P-frame was dropped: the stream is broken until the next keyframe.
    var onFrameDropped: (() -> Void)?
    var onListenError: ((Error) -> Void)?

    private let queue = DispatchQueue(label: "webicam.server", qos: .userInteractive)
    private var listener: NWListener?
    private var connection: NWConnection?
    private var parser = MessageParser()
    private var pendingSends = 0
    private var awaitingKeyframe = false

    /// Starts listening; safe to call again (e.g. when the app returns to the foreground):
    /// iOS invalidates listening sockets of suspended apps.
    func start() {
        queue.async { [weak self] in
            guard let self else { return }
            do {
                try self.listen()
            } catch {
                self.onListenError?(error)
            }
        }
    }

    private func listen() throws {
        if let listener {
            switch listener.state {
            case .setup, .waiting, .ready: return
            default: break
            }
        }
        listener?.cancel()
        listener = nil
        let tcp = NWProtocolTCP.Options()
        tcp.noDelay = true
        let parameters = NWParameters(tls: nil, tcp: tcp)
        parameters.allowLocalEndpointReuse = true
        parameters.requiredLocalEndpoint = .hostPort(host: "127.0.0.1",
                                                     port: NWEndpoint.Port(rawValue: ProtocolConstants.phonePort)!)
        let listener = try NWListener(using: parameters)
        listener.newConnectionHandler = { [weak self] connection in self?.accept(connection) }
        listener.stateUpdateHandler = { [weak self, weak listener] state in
            guard case .failed = state, let self, let listener, listener === self.listener else { return }
            listener.cancel()
            self.listener = nil
            self.queue.asyncAfter(deadline: .now() + 0.5) { try? self.listen() }
        }
        listener.start(queue: queue)
        self.listener = listener
    }

    func send(_ message: Message) {
        queue.async { [weak self] in
            guard let self, let connection = self.connection else { return }
            self.transmit(message, on: connection)
        }
    }

    /// While two sends are still in flight a P-frame is dropped instead of queued, so a slow
    /// link never builds latency. A drop breaks the reference chain, so every following
    /// P-frame is dropped too until a keyframe (requested via onFrameDropped) goes out.
    func sendVideo(_ message: Message, keyframe: Bool) {
        queue.async { [weak self] in
            guard let self, let connection = self.connection else { return }
            if keyframe {
                self.awaitingKeyframe = false
            } else if self.awaitingKeyframe {
                return
            } else if self.pendingSends >= 2 {
                self.awaitingKeyframe = true
                self.onFrameDropped?()
                return
            }
            self.transmit(message, on: connection)
        }
    }

    private func transmit(_ message: Message, on connection: NWConnection) {
        pendingSends += 1
        connection.send(content: message.encoded(), completion: .contentProcessed { [weak self, weak connection] _ in
            self?.queue.async {
                // Only count completions of the current connection.
                guard let self, let connection, connection === self.connection else { return }
                self.pendingSends -= 1
            }
        })
    }

    private func accept(_ newConnection: NWConnection) {
        connection?.cancel()
        connection = newConnection
        parser = MessageParser()
        pendingSends = 0
        awaitingKeyframe = false
        newConnection.stateUpdateHandler = { [weak self, weak newConnection] state in
            guard let self, let newConnection, newConnection === self.connection else { return }
            switch state {
            case .ready:
                self.onConnectionChange?(true)
                self.receive(on: newConnection)
            case .failed, .cancelled:
                self.connection = nil
                self.onConnectionChange?(false)
            default:
                break
            }
        }
        newConnection.start(queue: queue)
    }

    private func receive(on connection: NWConnection) {
        connection.receive(minimumIncompleteLength: 1, maximumLength: 65536) { [weak self] data, _, isComplete, error in
            guard let self, connection === self.connection else { return }
            if let data { self.parser.feed(data) }
            do {
                while let message = try self.parser.next() { self.onMessage?(message) }
            } catch {
                connection.cancel()
                return
            }
            if isComplete || error != nil {
                connection.cancel()
            } else {
                self.receive(on: connection)
            }
        }
    }
}
