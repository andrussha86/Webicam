import Foundation
import Network
import WebicamCore

/// TCP server on 127.0.0.1:47800 — reachable only through the usbmux USB tunnel.
/// One client at a time; a new connection replaces the old one.
final class StreamServer {
    var onMessage: ((Message) -> Void)?
    var onConnectionChange: ((Bool) -> Void)?

    private let queue = DispatchQueue(label: "webicam.server", qos: .userInteractive)
    private var listener: NWListener?
    private var connection: NWConnection?
    private var parser = MessageParser()
    private var pendingSends = 0

    func start() throws {
        let tcp = NWProtocolTCP.Options()
        tcp.noDelay = true
        let parameters = NWParameters(tls: nil, tcp: tcp)
        parameters.allowLocalEndpointReuse = true
        parameters.requiredLocalEndpoint = .hostPort(host: "127.0.0.1",
                                                     port: NWEndpoint.Port(rawValue: ProtocolConstants.phonePort)!)
        let listener = try NWListener(using: parameters)
        listener.newConnectionHandler = { [weak self] connection in self?.accept(connection) }
        listener.start(queue: queue)
        self.listener = listener
    }

    /// Video frames are `droppable`: skipped while two sends are still in flight,
    /// so a slow link never builds latency.
    func send(_ message: Message, droppable: Bool = false) {
        queue.async { [weak self] in
            guard let self, let connection = self.connection else { return }
            if droppable && self.pendingSends >= 2 { return }
            self.pendingSends += 1
            connection.send(content: message.encoded(), completion: .contentProcessed { [weak self] _ in
                self?.queue.async { self?.pendingSends -= 1 }
            })
        }
    }

    private func accept(_ newConnection: NWConnection) {
        connection?.cancel()
        connection = newConnection
        parser = MessageParser()
        pendingSends = 0
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
