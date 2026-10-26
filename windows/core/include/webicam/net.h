#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace webicam {

// Move-only RAII TCP socket (Winsock). WSAStartup runs once per process.
class Socket {
 public:
  static std::optional<Socket> ConnectTcp(const char* host, uint16_t port, int timeoutMs);
  static std::optional<Socket> Listen(const char* host, uint16_t port);

  Socket() = default;
  Socket(Socket&& other) noexcept;
  Socket& operator=(Socket&& other) noexcept;
  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;
  ~Socket();

  // Listening socket only; returns an invalid socket on timeout or error.
  Socket Accept(int timeoutMs = -1);
  bool SendAll(std::span<const uint8_t> bytes);
  // >0 bytes read, 0 closed by peer, -1 error, -2 timeout.
  int Recv(std::span<uint8_t> buffer, int timeoutMs);
  // Reads exactly buffer.size() bytes or fails.
  bool RecvAll(std::span<uint8_t> buffer, int timeoutMs);
  void Shutdown();
  uint16_t LocalPort() const;
  bool valid() const { return handle_ != kInvalid; }

 private:
  static constexpr uintptr_t kInvalid = ~uintptr_t(0);
  explicit Socket(uintptr_t handle) : handle_(handle) {}
  void Close();

  uintptr_t handle_ = kInvalid;
};

}  // namespace webicam
