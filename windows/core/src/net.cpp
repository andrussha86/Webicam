#include "webicam/net.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <mutex>
#include <utility>

namespace webicam {

namespace {

void EnsureWinsock() {
  static std::once_flag once;
  std::call_once(once, [] {
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
  });
}

bool WaitFor(SOCKET s, bool forWrite, int timeoutMs) {
  WSAPOLLFD fd{s, short(forWrite ? POLLWRNORM : POLLRDNORM), 0};
  const int r = WSAPoll(&fd, 1, timeoutMs);
  return r > 0;
}

sockaddr_in Address(const char* host, uint16_t port) {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, host, &addr.sin_addr) == 1) return addr;
  // A name such as "iPhone.local": Windows resolves .local names over mDNS too.
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* found = nullptr;
  if (getaddrinfo(host, nullptr, &hints, &found) == 0 && found) {
    addr.sin_addr = reinterpret_cast<const sockaddr_in*>(found->ai_addr)->sin_addr;
    freeaddrinfo(found);
  }
  return addr;
}

}  // namespace

std::optional<Socket> Socket::ConnectTcp(const char* host, uint16_t port, int timeoutMs) {
  EnsureWinsock();
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return std::nullopt;
  Socket result(s);
  u_long nonBlocking = 1;
  ioctlsocket(s, FIONBIO, &nonBlocking);
  auto addr = Address(host, port);
  if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    if (WSAGetLastError() != WSAEWOULDBLOCK) return std::nullopt;
    if (!WaitFor(s, true, timeoutMs)) return std::nullopt;
    int error = 0;
    int length = sizeof(error);
    getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length);
    if (error != 0) return std::nullopt;
  }
  nonBlocking = 0;
  ioctlsocket(s, FIONBIO, &nonBlocking);
  BOOL noDelay = TRUE;
  setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
  return result;
}

std::optional<Socket> Socket::Listen(const char* host, uint16_t port) {
  EnsureWinsock();
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return std::nullopt;
  Socket result(s);
  auto addr = Address(host, port);
  if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return std::nullopt;
  if (listen(s, 4) != 0) return std::nullopt;
  return result;
}

Socket::Socket(Socket&& other) noexcept : handle_(std::exchange(other.handle_, kInvalid)) {}

Socket& Socket::operator=(Socket&& other) noexcept {
  if (this != &other) {
    Close();
    handle_ = std::exchange(other.handle_, kInvalid);
  }
  return *this;
}

Socket::~Socket() { Close(); }

void Socket::Close() {
  if (handle_ != kInvalid) closesocket(SOCKET(handle_));
  handle_ = kInvalid;
}

Socket Socket::Accept(int timeoutMs) {
  if (!valid()) return {};
  if (timeoutMs >= 0 && !WaitFor(SOCKET(handle_), false, timeoutMs)) return {};
  SOCKET s = accept(SOCKET(handle_), nullptr, nullptr);
  if (s == INVALID_SOCKET) return {};
  BOOL noDelay = TRUE;
  setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
  return Socket(s);
}

bool Socket::SendAll(std::span<const uint8_t> bytes) {
  while (!bytes.empty()) {
    const int sent = send(SOCKET(handle_), reinterpret_cast<const char*>(bytes.data()),
                          int(std::min<size_t>(bytes.size(), 1 << 20)), 0);
    if (sent <= 0) return false;
    bytes = bytes.subspan(size_t(sent));
  }
  return true;
}

int Socket::Recv(std::span<uint8_t> buffer, int timeoutMs) {
  if (!valid()) return -1;
  if (timeoutMs >= 0 && !WaitFor(SOCKET(handle_), false, timeoutMs)) return -2;
  const int r = recv(SOCKET(handle_), reinterpret_cast<char*>(buffer.data()), int(buffer.size()), 0);
  return r < 0 ? -1 : r;
}

bool Socket::RecvAll(std::span<uint8_t> buffer, int timeoutMs) {
  while (!buffer.empty()) {
    const int r = Recv(buffer, timeoutMs);
    if (r <= 0) return false;
    buffer = buffer.subspan(size_t(r));
  }
  return true;
}

void Socket::Shutdown() {
  if (valid()) shutdown(SOCKET(handle_), SD_BOTH);
}

uint16_t Socket::LocalPort() const {
  sockaddr_in addr{};
  int length = sizeof(addr);
  getsockname(SOCKET(handle_), reinterpret_cast<sockaddr*>(&addr), &length);
  return ntohs(addr.sin_port);
}

}  // namespace webicam
