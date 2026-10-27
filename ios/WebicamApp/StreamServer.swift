import Foundation
import Network
import WebicamCore

/// How the current PC reached the phone.
enum PhoneLink {
    case usb, wifi
}

/// TCP server on port 47800. Loopback peers come through the usbmux USB tunnel and are taken at once;
/// any other peer is a PC on the local network and must send the code from the screen first
/// (see shared/protocol.md, Wi-Fi pairing). The service is advertised over Bonjour as `_webicam._tcp`.
/// One client at a time; an accepted connection replaces the old one.
final class StreamServer {
    static let bonjourType = "_webicam._tcp"

    var onMessage: ((Message) -> Void)?
    /// The link of the new client, or nil when the client is gone.
    var onConnectionChange: ((PhoneLink?) -> Void)?
    /// A P-frame was dropped: the stream is broken until the next keyframe.
    var onFrameDropped: (() -> Void)?
    /// nil once the port is open, otherwise why it is not.
    var onListenStateChange: ((Error?) -> Void)?
    /// Advertising on the network failed (usually no local network permission): USB only from now on.
    var onWifiUnavailable: (() -> Void)?

    private let queue = DispatchQueue(label: "webicam.server", qos: .userInteractive)
    private let phoneId: String
    private var pairingCode: String
    private var listener: NWListener?
    private var connection: NWConnection?
    private var parser = MessageParser()
    private var pendingSends = 0
    private var awaitingKeyframe = false
    /// A Wi-Fi peer that has not sent the right code yet.
    private var pending: NWConnection?
    private var pendingParser = MessageParser()
    /// Cleared when the Bonjour service cannot be registered, so the USB port still opens.
    private var advertise = true
    /// After a wrong code no code is checked for a while, so six digits cannot be guessed quickly.
    private var pairingLockedUntil = DispatchTime.now()
    private static let pairingLockout = DispatchTimeInterval.seconds(2)

    init(phoneId: String, pairingCode: String) {
        self.phoneId = phoneId
        self.pairingCode = pairingCode
    }

    /// Takes effect for the next Wi-Fi peer; the current client stays.
    func setPairingCode(_ code: String) {
        queue.async { [weak self] in self?.pairingCode = code }
    }

    /// Starts listening; safe to call again (e.g. when the app returns to the foreground):
    /// iOS invalidates listening sockets of suspended apps.
    func start() {
        queue.async { [weak self] in
            guard let self else { return }
            do {
                try self.listen()
            } catch {
                self.onListenStateChange?(error)
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
        // Binding to 127.0.0.1 via requiredLocalEndpoint left the port closed for usbmux
        // (Connect → Number 3), so listen on every interface and filter peers in accept().
        let listener = try NWListener(using: parameters,
                                      on: NWEndpoint.Port(rawValue: ProtocolConstants.phonePort)!)
        if advertise {
            listener.service = NWListener.Service(name: nil, type: Self.bonjourType, domain: nil,
                                                  txtRecord: NWTXTRecord(["id": phoneId]))
        }
        listener.newConnectionHandler = { [weak self] connection in self?.accept(connection) }
        listener.stateUpdateHandler = { [weak self, weak listener] state in
            guard let self, let listener, listener === self.listener else { return }
            switch state {
            case .ready:
                self.onListenStateChange?(nil)
            case .waiting where self.advertise, .failed where self.advertise:
                // A denied local network permission must not cost the cable: listen again without Bonjour.
                self.advertise = false
                listener.cancel()
                self.listener = nil
                self.onWifiUnavailable?()
                do { try self.listen() } catch { self.onListenStateChange?(error) }
            case let .waiting(error):
                self.onListenStateChange?(error)
            case let .failed(error):
                self.onListenStateChange?(error)
                listener.cancel()
                self.listener = nil
                self.queue.asyncAfter(deadline: .now() + 0.5) {
                    do { try self.listen() } catch { self.onListenStateChange?(error) }
                }
            default:
                break
            }
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

    /// Audio is small and must not be dropped, so it does not count towards the video's in-flight limit.
    func sendAudio(_ message: Message) {
        queue.async { [weak self] in
            guard let self, let connection = self.connection else { return }
            connection.send(content: message.encoded(), completion: .idempotent)
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
        let usb = Self.isLoopback(newConnection.endpoint)
        if usb {
            take(newConnection, link: .usb, parser: MessageParser())
        } else {
            pending?.cancel()
            pending = newConnection
            pendingParser = MessageParser()
            // Too slow to pair: most likely not a Webicam PC at all.
            queue.asyncAfter(deadline: .now() + 5) { [weak self, weak newConnection] in
                guard let self, let newConnection, newConnection === self.pending else { return }
                self.reject(newConnection, "Компьютер не прислал код")
            }
        }
        newConnection.stateUpdateHandler = { [weak self, weak newConnection] state in
            guard let self, let newConnection else { return }
            switch state {
            case .ready:
                if newConnection === self.connection {
                    self.onConnectionChange?(.usb)
                    self.receive(on: newConnection)
                } else if newConnection === self.pending {
                    self.receivePairing(on: newConnection)
                }
            case .failed, .cancelled:
                if newConnection === self.connection {
                    self.connection = nil
                    self.onConnectionChange?(nil)
                } else if newConnection === self.pending {
                    self.pending = nil
                }
            default:
                break
            }
        }
        newConnection.start(queue: queue)
    }

    /// Makes `newConnection` the client; the previous one is dropped without a disconnect event.
    private func take(_ newConnection: NWConnection, link: PhoneLink, parser: MessageParser) {
        let old = connection
        connection = newConnection
        self.parser = parser
        pendingSends = 0
        awaitingKeyframe = false
        old?.cancel()
        if link == .wifi {
            pending = nil
            onConnectionChange?(.wifi)
            receive(on: newConnection)
        }
    }

    /// Reads the Wi-Fi peer's Hello and Pair; nothing is sent to it before the code matches.
    private func receivePairing(on peer: NWConnection) {
        peer.receive(minimumIncompleteLength: 1, maximumLength: 4096) { [weak self] data, _, isComplete, error in
            guard let self, peer === self.pending else { return }
            if let data { self.pendingParser.feed(data) }
            do {
                while let message = try self.pendingParser.next() {
                    switch message {
                    case .hello:
                        continue
                    case .pair where DispatchTime.now() < self.pairingLockedUntil:
                        self.reject(peer, "Слишком много попыток, подождите пару секунд")
                        return
                    case let .pair(code) where code == self.pairingCode:
                        self.take(peer, link: .wifi, parser: self.pendingParser)
                        return
                    case .pair:
                        self.pairingLockedUntil = DispatchTime.now() + Self.pairingLockout
                        self.reject(peer, "Неверный код. Введите код с экрана iPhone")
                        return
                    default:
                        self.reject(peer, "Нужен код с экрана iPhone")
                        return
                    }
                }
            } catch {
                peer.cancel()
                return
            }
            if isComplete || error != nil {
                peer.cancel()
            } else {
                self.receivePairing(on: peer)
            }
        }
    }

    private func reject(_ peer: NWConnection, _ reason: String) {
        pending = nil
        let status = Message.status(state: .pairingRejected, thermal: 0, message: reason).encoded()
        peer.send(content: status, completion: .contentProcessed { _ in peer.cancel() })
    }

    /// usbmux delivers tunnelled connections from the phone's own loopback; anything else
    /// is a Wi-Fi peer that has to pair first.
    private static func isLoopback(_ endpoint: NWEndpoint) -> Bool {
        guard case let .hostPort(host, _) = endpoint else { return false }
        switch host {
        case let .ipv4(address):
            return address.isLoopback
        case let .ipv6(address):
            // An IPv4 peer on a dual-stack socket shows up as ::ffff:127.x.x.x.
            return address.isLoopback || address.asIPv4?.isLoopback == true
        default:
            return false
        }
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
