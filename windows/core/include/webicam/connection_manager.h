#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "webicam/device_session.h"
#include "webicam/usbmux.h"
#include "webicam/wifi_browser.h"

namespace webicam {

enum class ConnState {
  AppleServiceMissing, NoDevice, ChooseDevice, PhoneAppNotOpen, Connecting, Streaming, VersionMismatch,
  WifiNeedsCode  // a Wi-Fi phone was found but its code is unknown or was refused; see CodeRequest()
};

// Which links to try. Auto prefers the cable and moves to it from Wi-Fi when one is plugged in.
enum class LinkMode : uint8_t { Auto = 0, Usb = 1, Wifi = 2 };
inline constexpr int kLinkModeCount = 3;

// How the current session reaches the phone.
enum class Transport { None, Usb, Wifi, Direct };

// Finds the iPhone over usbmux or on the local network (or a direct TCP endpoint), keeps one
// DeviceSession alive and reconnects when it drops. Runs its own thread.
class ConnectionManager {
 public:
  struct Callbacks {
    std::function<void(proto::VideoFrame&&)> onFrame;      // session thread
    std::function<void(bool connected)> onConnectionChanged;
    std::function<void(const proto::Status&)> onStatus;
    std::function<void(proto::AudioFrame&&)> onAudio;  // session thread
  };

  static constexpr int kWifiIdleTimeoutMs = 5000;  // Wi-Fi stalls for longer than a cable

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

  void SetLinkMode(LinkMode mode);
  LinkMode GetLinkMode() const;
  Transport CurrentTransport() const;
  // Phones found on the network plus the typed address, if any.
  std::vector<WifiPhone> WifiPhones() const;
  void SelectWifiPhone(const std::string& id);
  // "host" or "host:port" typed by the user for networks where Bonjour does not get through; "" = none.
  void SetWifiAddress(std::string address);
  std::string WifiAddress() const;
  // Codes known per phone id; remembered by the app between runs.
  void SetWifiCodes(std::map<std::string, std::string> codes);
  std::map<std::string, std::string> WifiCodes() const;
  // Remembers the code, selects the phone and connects to it right away.
  void SetWifiCode(const std::string& phoneId, std::string code);
  // Id of the phone on the current Wi-Fi session.
  std::optional<std::string> ActiveWifiId() const;
  // The phone whose code is needed while State() is WifiNeedsCode.
  std::optional<WifiPhone> CodeRequest() const;
  // Remembered and re-sent after every Configure, like the camera settings.
  void SetMicEnabled(bool enabled);
  bool MicEnabled() const;
  // Last Telemetry on the current connection.
  std::optional<proto::Telemetry> PhoneTelemetry() const;
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
  // True when a session was started; otherwise the state says why not.
  bool TryUsb(bool quiet);
  void TryWifi(bool usbAvailable);
  // Auto mode on Wi-Fi: moves to the cable as soon as the phone answers on it.
  void PreferUsb();
  void DropSession(std::unique_ptr<DeviceSession> dead);
  void Attach(Socket socket, Transport transport, std::optional<WifiPhone> wifi = std::nullopt,
              std::optional<std::string> code = std::nullopt);
  void SetState(ConnState state, std::string detail);
  // Configure (optional), then the background picture when the mode needs it, then CameraSettings;
  // the picture is rendered before the lock is taken.
  void SendSetup(std::optional<proto::Configure> configure, bool sendPicture);

  std::function<std::vector<uint8_t>(uint16_t, uint16_t)> backgroundSource_;

  Callbacks callbacks_;
  UsbmuxClient usbmux_;
  WifiBrowser browser_;
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
  LinkMode linkMode_ = LinkMode::Auto;
  Transport transport_ = Transport::None;
  std::string wifiAddress_;
  std::map<std::string, std::string> wifiCodes_;
  std::optional<std::string> selectedWifi_;
  std::optional<WifiPhone> wifiPeer_;     // the phone of the current Wi-Fi session
  std::optional<WifiPhone> codeRequest_;  // phone waiting for a code
  std::string pairingRejected_;           // reason, set until the session is dropped
  std::optional<proto::Telemetry> telemetry_;
  bool retryNow_ = false;
  bool micEnabled_ = false;
  uint64_t lastUsbProbeMs_ = 0;

  std::atomic<bool> stop_{false};
  std::thread thread_;
};

}  // namespace webicam
