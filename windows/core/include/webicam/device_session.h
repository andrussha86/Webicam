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
    std::function<void(const proto::CameraState&)> onCameraState;
    std::function<void(const proto::Telemetry&)> onTelemetry;
    std::function<void(proto::AudioFrame&&)> onAudio;
    // A Wi-Fi phone refused the code (before any Hello); onClosed follows.
    std::function<void(const std::string& reason)> onPairingRejected;
    // Fires exactly once when the phone side closes or misbehaves; never from the destructor.
    // Must not destroy the session from inside the callback.
    std::function<void(std::string reason)> onClosed;
  };

  // Closes the session when nothing (frames, pongs) arrives for idleTimeoutMs —
  // a suspended iPhone app can leave the USB tunnel open but silent.
  static constexpr int kDefaultIdleTimeoutMs = 3000;

  DeviceSession(Socket socket, Events events, int idleTimeoutMs = kDefaultIdleTimeoutMs);
  DeviceSession(const DeviceSession&) = delete;
  DeviceSession& operator=(const DeviceSession&) = delete;
  ~DeviceSession();

  // Sends Hello (and Pair right after it for a Wi-Fi phone), then starts reading.
  void Start(std::optional<std::string> pairingCode = std::nullopt);
  void SendConfigure(const proto::Configure& configure);
  void SendCameraSettings(const proto::CameraSettings& settings);
  void SendBackgroundImage(std::vector<uint8_t> jpeg);
  void SendMicSettings(const proto::MicSettings& settings);
  void RequestKeyframe();
  std::optional<int64_t> PhoneToPcUs(uint64_t phoneUs) const;
  bool Closed() const { return closed_.load(); }

 private:
  void Run();
  bool Send(const proto::Message& message);
  void Close(std::string reason);

  Socket socket_;
  Events events_;
  int idleTimeoutMs_;
  std::thread thread_;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> closed_{false};
  std::mutex sendMutex_;
  mutable std::mutex clockMutex_;
  ClockSync clock_;
};

}  // namespace webicam
