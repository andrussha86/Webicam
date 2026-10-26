#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "webicam/clock_sync.h"
#include "webicam/net.h"
#include "webicam/protocol.h"

namespace webicam {

// Microseconds from QueryPerformanceCounter.
uint64_t NowUs();

// One connection to the phone app: handshake, frames, control, ping/clock sync.
// Events fire on the session's own thread.
class DeviceSession {
 public:
  struct Events {
    std::function<void(const proto::Hello&)> onHello;
    std::function<void(proto::VideoFrame&&)> onFrame;
    std::function<void(const proto::Status&)> onStatus;
    // Fires exactly once when the phone side closes or misbehaves; never from the destructor.
    // Must not destroy the session from inside the callback.
    std::function<void(std::string reason)> onClosed;
  };

  DeviceSession(Socket socket, Events events);
  DeviceSession(const DeviceSession&) = delete;
  DeviceSession& operator=(const DeviceSession&) = delete;
  ~DeviceSession();

  void Start();
  void SendConfigure(const proto::Configure& configure);
  void RequestKeyframe();
  std::optional<int64_t> PhoneToPcUs(uint64_t phoneUs) const;
  bool Closed() const { return closed_.load(); }

 private:
  void Run();
  bool Send(const proto::Message& message);
  void Close(std::string reason);

  Socket socket_;
  Events events_;
  std::thread thread_;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> closed_{false};
  std::mutex sendMutex_;
  mutable std::mutex clockMutex_;
  ClockSync clock_;
};

}  // namespace webicam
