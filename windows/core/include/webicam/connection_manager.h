#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "webicam/device_session.h"
#include "webicam/usbmux.h"

namespace webicam {

enum class ConnState { AppleServiceMissing, NoDevice, ChooseDevice, PhoneAppNotOpen, Connecting, Streaming, VersionMismatch };

// Finds the iPhone over usbmux (or a direct TCP endpoint), keeps one DeviceSession
// alive and reconnects when it drops. Runs its own thread.
class ConnectionManager {
 public:
  struct Callbacks {
    std::function<void(proto::VideoFrame&&)> onFrame;      // session thread
    std::function<void(bool connected)> onConnectionChanged;
    std::function<void(const proto::Status&)> onStatus;
  };

  explicit ConnectionManager(Callbacks callbacks, uint16_t usbmuxPort = UsbmuxClient::kDefaultPort);
  ConnectionManager(const ConnectionManager&) = delete;
  ConnectionManager& operator=(const ConnectionManager&) = delete;
  ~ConnectionManager();

  // Must be called before Start(): bypass usbmux and connect to host:port.
  void SetDirect(std::string host, uint16_t port);
  void Start();
  // Joins the worker thread and drops the session; no callbacks fire afterwards.
  void Stop();

  ConnState State() const;
  std::string Detail() const;  // Russian status line for the UI
  std::vector<UsbmuxDevice> Devices() const;
  void SelectDevice(uint32_t deviceId);
  std::vector<proto::CaptureFormat> PhoneFormats() const;
  std::string Model() const;

  // Remembered and re-sent after every reconnect.
  void SetDesiredConfigure(const proto::Configure& configure);
  proto::Configure DesiredConfigure() const;
  // Remembered and re-sent after every Configure.
  void SetDesiredCamera(const proto::CameraSettings& settings);
  proto::CameraSettings DesiredCamera() const;
  // Last CameraState from the phone; nullopt until one arrives on the current connection.
  std::optional<proto::CameraState> PhoneCamera() const;
  // JPEG of the background picture scaled to width×height (empty = none). Called whenever the phone
  // needs the picture, never under the manager's lock. Set before Start().
  void SetBackgroundSource(std::function<std::vector<uint8_t>(uint16_t width, uint16_t height)> source);
  // The picture changed: resend it if the background mode is Image and a phone is streaming.
  void InvalidateBackground();
  void RequestKeyframe();
  std::optional<int64_t> PhoneToPcUs(uint64_t phoneUs) const;

 private:
  void Run();
  void TryConnect();
  void Attach(Socket socket);
  void SetState(ConnState state, std::string detail);
  // Configure (optional), then the background picture when the mode needs it, then CameraSettings;
  // the picture is rendered before the lock is taken.
  void SendSetup(std::optional<proto::Configure> configure, bool sendPicture);

  std::function<std::vector<uint8_t>(uint16_t, uint16_t)> backgroundSource_;

  Callbacks callbacks_;
  UsbmuxClient usbmux_;
  std::optional<std::pair<std::string, uint16_t>> direct_;

  mutable std::mutex mutex_;
  ConnState state_ = ConnState::NoDevice;
  std::string detail_ = "Поиск iPhone…";
  std::vector<UsbmuxDevice> devices_;
  std::optional<uint32_t> selected_;
  std::vector<proto::CaptureFormat> formats_;
  std::string model_;
  proto::Configure desired_{1920, 1080, 30, 16000};
  bool desiredExplicit_ = false;
  proto::CameraSettings camera_;
  std::optional<proto::CameraState> phoneCamera_;
  std::unique_ptr<DeviceSession> session_;
  bool streaming_ = false;
  std::string closeReason_;

  std::atomic<bool> stop_{false};
  std::thread thread_;
};

}  // namespace webicam
