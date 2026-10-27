#include "webicam/connection_manager.h"

#include "webicam/vcam_logic.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <utility>

namespace webicam {

namespace {

constexpr auto kPollInterval = std::chrono::milliseconds(250);
constexpr uint64_t kRetryMs = 1500;
constexpr uint64_t kUsbProbeMs = 3000;
constexpr int kWifiConnectTimeoutMs = 1500;

// Default capture format: 1080p at the highest rate ≤ 60 the phone offers.
std::optional<proto::Configure> DefaultConfigure(const std::vector<proto::CaptureFormat>& formats) {
  for (const auto& f : formats) {
    if (f.width == 1920 && f.height == 1080) {
      const uint8_t fps = std::min<uint8_t>(f.maxFps, 60);
      return proto::Configure{1920, 1080, fps, proto::DefaultBitrateKbps(1080, fps)};
    }
  }
  return std::nullopt;
}

// "host" or "host:port" -> the phone it names; nullopt when empty or the port is not a number.
std::optional<WifiPhone> TypedPhone(const std::string& address) {
  if (address.empty()) return std::nullopt;
  WifiPhone phone{"addr:" + address, address, address, proto::kPhonePort};
  const auto colon = address.rfind(':');
  if (colon != std::string::npos) {
    unsigned port = 0;
    const char* first = address.data() + colon + 1;
    const char* last = address.data() + address.size();
    const auto [end, ec] = std::from_chars(first, last, port);
    if (ec != std::errc() || end != last || port == 0 || port > 65535 || colon == 0) return std::nullopt;
    phone.host = address.substr(0, colon);
    phone.port = uint16_t(port);
  }
  return phone;
}

}  // namespace

ConnectionManager::ConnectionManager(Callbacks callbacks, uint16_t usbmuxPort)
    : callbacks_(std::move(callbacks)), usbmux_(usbmuxPort) {}

ConnectionManager::~ConnectionManager() { Stop(); }

void ConnectionManager::Stop() {
  stop_ = true;
  browser_.Stop();
  if (thread_.joinable()) thread_.join();
  std::unique_ptr<DeviceSession> session;
  {
    std::lock_guard lock(mutex_);
    session = std::move(session_);
  }
  session.reset();
}

void ConnectionManager::SetDirect(std::string host, uint16_t port) { direct_ = {std::move(host), port}; }

void ConnectionManager::Start() {
  if (!direct_) browser_.Start();
  thread_ = std::thread([this] { Run(); });
}

ConnState ConnectionManager::State() const {
  std::lock_guard lock(mutex_);
  return state_;
}

std::string ConnectionManager::Detail() const {
  std::lock_guard lock(mutex_);
  return detail_;
}

std::vector<UsbmuxDevice> ConnectionManager::Devices() const {
  std::lock_guard lock(mutex_);
  return devices_;
}

void ConnectionManager::SelectDevice(uint32_t deviceId) {
  std::lock_guard lock(mutex_);
  selected_ = deviceId;
}

std::vector<proto::CaptureFormat> ConnectionManager::PhoneFormats() const {
  std::lock_guard lock(mutex_);
  return formats_;
}

std::string ConnectionManager::Model() const {
  std::lock_guard lock(mutex_);
  return model_;
}

void ConnectionManager::SetDesiredConfigure(const proto::Configure& configure) {
  {
    std::lock_guard lock(mutex_);
    desired_ = configure;
    desiredExplicit_ = true;
  }
  SendSetup(configure, true);
}

void ConnectionManager::SetDesiredCamera(const proto::CameraSettings& settings) {
  bool pictureNeeded = false;
  {
    std::lock_guard lock(mutex_);
    if (settings == camera_) return;
    pictureNeeded = settings.backgroundMode == proto::BackgroundMode::Image &&
                    camera_.backgroundMode != proto::BackgroundMode::Image;
    camera_ = settings;
  }
  SendSetup(std::nullopt, pictureNeeded);
}

void ConnectionManager::SetBackgroundSource(std::function<std::vector<uint8_t>(uint16_t, uint16_t)> source) {
  backgroundSource_ = std::move(source);
}

void ConnectionManager::InvalidateBackground() {
  proto::Configure size;
  {
    std::lock_guard lock(mutex_);
    if (!session_ || !streaming_ || camera_.backgroundMode != proto::BackgroundMode::Image) return;
    size = desired_;
  }
  auto jpeg = backgroundSource_ ? backgroundSource_(size.width, size.height) : std::vector<uint8_t>{};
  std::lock_guard lock(mutex_);
  if (session_ && streaming_ && camera_.backgroundMode == proto::BackgroundMode::Image) {
    session_->SendBackgroundImage(std::move(jpeg));
  }
}

void ConnectionManager::SendSetup(std::optional<proto::Configure> configure, bool sendPicture) {
  proto::Configure size;
  bool picture = false;
  {
    std::lock_guard lock(mutex_);
    if (!session_ || !streaming_) return;
    size = desired_;
    picture = sendPicture && camera_.backgroundMode == proto::BackgroundMode::Image;
  }
  std::vector<uint8_t> jpeg;
  if (picture && backgroundSource_) jpeg = backgroundSource_(size.width, size.height);
  std::lock_guard lock(mutex_);
  if (!session_ || !streaming_) return;
  if (configure) session_->SendConfigure(*configure);
  // Mode may have changed while rendering; the settings below always win on the phone.
  if (picture && camera_.backgroundMode == proto::BackgroundMode::Image) {
    session_->SendBackgroundImage(std::move(jpeg));
  }
  session_->SendCameraSettings(camera_);
  session_->SendMicSettings({micEnabled_});
}

void ConnectionManager::SetMicEnabled(bool enabled) {
  std::lock_guard lock(mutex_);
  if (micEnabled_ == enabled) return;
  micEnabled_ = enabled;
  if (session_ && streaming_) session_->SendMicSettings({enabled});
}

bool ConnectionManager::MicEnabled() const {
  std::lock_guard lock(mutex_);
  return micEnabled_;
}

proto::CameraSettings ConnectionManager::DesiredCamera() const {
  std::lock_guard lock(mutex_);
  return camera_;
}

std::optional<proto::CameraState> ConnectionManager::PhoneCamera() const {
  std::lock_guard lock(mutex_);
  return phoneCamera_;
}

proto::Configure ConnectionManager::DesiredConfigure() const {
  std::lock_guard lock(mutex_);
  return desired_;
}

void ConnectionManager::RequestKeyframe() {
  std::lock_guard lock(mutex_);
  if (session_) session_->RequestKeyframe();
}

std::optional<int64_t> ConnectionManager::PhoneToPcUs(uint64_t phoneUs) const {
  std::lock_guard lock(mutex_);
  return session_ ? session_->PhoneToPcUs(phoneUs) : std::nullopt;
}

void ConnectionManager::SetLinkMode(LinkMode mode) {
  std::unique_ptr<DeviceSession> dead;
  {
    std::lock_guard lock(mutex_);
    if (linkMode_ == mode) return;
    linkMode_ = mode;
    retryNow_ = true;
    // A session on a link that is no longer allowed ends now.
    const bool allowed = mode == LinkMode::Auto || transport_ == Transport::Direct ||
                         (mode == LinkMode::Usb) == (transport_ == Transport::Usb);
    if (session_ && !allowed) dead = std::move(session_);
    codeRequest_.reset();
  }
  if (dead) DropSession(std::move(dead));
}

LinkMode ConnectionManager::GetLinkMode() const {
  std::lock_guard lock(mutex_);
  return linkMode_;
}

Transport ConnectionManager::CurrentTransport() const {
  std::lock_guard lock(mutex_);
  return session_ && streaming_ ? transport_ : Transport::None;
}

std::vector<WifiPhone> ConnectionManager::WifiPhones() const {
  auto phones = browser_.Phones();
  std::lock_guard lock(mutex_);
  if (auto typed = TypedPhone(wifiAddress_)) phones.push_back(*typed);
  return phones;
}

void ConnectionManager::SelectWifiPhone(const std::string& id) {
  std::lock_guard lock(mutex_);
  selectedWifi_ = id;
  retryNow_ = true;
}

void ConnectionManager::SetWifiAddress(std::string address) {
  std::lock_guard lock(mutex_);
  if (wifiAddress_ == address) return;
  wifiAddress_ = std::move(address);
  if (auto typed = TypedPhone(wifiAddress_)) selectedWifi_ = typed->id;
  retryNow_ = true;
}

std::string ConnectionManager::WifiAddress() const {
  std::lock_guard lock(mutex_);
  return wifiAddress_;
}

void ConnectionManager::SetWifiCodes(std::map<std::string, std::string> codes) {
  std::lock_guard lock(mutex_);
  wifiCodes_ = std::move(codes);
}

std::map<std::string, std::string> ConnectionManager::WifiCodes() const {
  std::lock_guard lock(mutex_);
  return wifiCodes_;
}

void ConnectionManager::SetWifiCode(const std::string& phoneId, std::string code) {
  std::lock_guard lock(mutex_);
  wifiCodes_[phoneId] = std::move(code);
  selectedWifi_ = phoneId;
  codeRequest_.reset();
  retryNow_ = true;
  if (!session_) {
    state_ = ConnState::Connecting;
    detail_ = "Подключение по Wi-Fi…";
  }
}

std::optional<std::string> ConnectionManager::ActiveWifiId() const {
  std::lock_guard lock(mutex_);
  if (!session_ || transport_ != Transport::Wifi || !wifiPeer_) return std::nullopt;
  return wifiPeer_->id;
}

std::optional<WifiPhone> ConnectionManager::CodeRequest() const {
  std::lock_guard lock(mutex_);
  return state_ == ConnState::WifiNeedsCode ? codeRequest_ : std::nullopt;
}

std::optional<proto::Telemetry> ConnectionManager::PhoneTelemetry() const {
  std::lock_guard lock(mutex_);
  return streaming_ ? telemetry_ : std::nullopt;
}

void ConnectionManager::SetState(ConnState state, std::string detail) {
  std::lock_guard lock(mutex_);
  state_ = state;
  detail_ = std::move(detail);
}

void ConnectionManager::Attach(Socket socket, Transport transport, std::optional<WifiPhone> wifi,
                               std::optional<std::string> code) {
  SetState(ConnState::Connecting, transport == Transport::Wifi ? "Подключение по Wi-Fi…" : "Подключение…");
  DeviceSession::Events events;
  events.onHello = [this](const proto::Hello& hello) {
    std::optional<proto::Configure> configure;
    {
      std::lock_guard lock(mutex_);
      formats_ = hello.formats;
      model_ = hello.model;
      if (!desiredExplicit_) {
        if (auto best = DefaultConfigure(hello.formats)) desired_ = *best;
      }
      configure = desired_;
      streaming_ = true;
      state_ = ConnState::Streaming;
      const std::string name =
          wifiPeer_ ? wifiPeer_->name : (hello.model.empty() ? std::string("iPhone") : hello.model);
      detail_ = "Подключено: " + name;
    }
    if (callbacks_.onConnectionChanged) callbacks_.onConnectionChanged(true);
    SendSetup(configure, true);
  };
  events.onFrame = [this](proto::VideoFrame&& frame) {
    if (callbacks_.onFrame) callbacks_.onFrame(std::move(frame));
  };
  events.onStatus = [this](const proto::Status& status) {
    if (callbacks_.onStatus) callbacks_.onStatus(status);
  };
  events.onCameraState = [this](const proto::CameraState& state) {
    std::lock_guard lock(mutex_);
    phoneCamera_ = state;
  };
  events.onAudio = [this](proto::AudioFrame&& frame) {
    if (callbacks_.onAudio) callbacks_.onAudio(std::move(frame));
  };
  events.onTelemetry = [this](const proto::Telemetry& telemetry) {
    std::lock_guard lock(mutex_);
    telemetry_ = telemetry;
  };
  events.onPairingRejected = [this](const std::string& reason) {
    std::lock_guard lock(mutex_);
    pairingRejected_ = reason.empty() ? std::string("Неверный код") : reason;
    if (wifiPeer_) wifiCodes_.erase(wifiPeer_->id);
  };
  events.onClosed = [this](std::string reason) {
    std::lock_guard lock(mutex_);
    closeReason_ = std::move(reason);
  };
  const int idleMs = transport == Transport::Wifi ? kWifiIdleTimeoutMs : DeviceSession::kDefaultIdleTimeoutMs;
  auto session = std::make_unique<DeviceSession>(std::move(socket), std::move(events), idleMs);
  DeviceSession* raw = session.get();
  {
    std::lock_guard lock(mutex_);
    session_ = std::move(session);
    transport_ = transport;
    wifiPeer_ = std::move(wifi);
    phoneCamera_.reset();
    telemetry_.reset();
    closeReason_.clear();
    pairingRejected_.clear();
    codeRequest_.reset();
  }
  raw->Start(std::move(code));
}

bool ConnectionManager::TryUsb(bool quiet) {
  auto devices = usbmux_.ListDevices();
  if (!devices) {
    if (quiet) return false;
    if (devices.error() == UsbmuxError::ServiceUnavailable) {
      SetState(ConnState::AppleServiceMissing, "Установите «Apple Devices» из Microsoft Store");
    } else {
      SetState(ConnState::NoDevice, "Служба Apple Mobile Device ответила с ошибкой");
    }
    return false;
  }
  std::optional<uint32_t> target;
  {
    std::lock_guard lock(mutex_);
    devices_ = *devices;
    if (selected_ && std::ranges::none_of(devices_, [&](const auto& d) { return d.deviceId == *selected_; })) {
      selected_.reset();
    }
    if (devices_.size() == 1) target = devices_[0].deviceId;
    if (selected_) target = selected_;
  }
  if (devices->empty()) {
    if (!quiet) SetState(ConnState::NoDevice, "Подключите iPhone кабелем");
    return false;
  }
  if (!target) {
    if (!quiet) SetState(ConnState::ChooseDevice, "Подключено несколько iPhone, выберите нужный");
    return false;
  }
  auto socket = usbmux_.Connect(*target, proto::kPhonePort);
  if (!socket) {
    if (quiet) return false;
    if (socket.error() == UsbmuxError::ConnectionRefused) {
      SetState(ConnState::PhoneAppNotOpen,
               "Откройте Webicam на iPhone. Если он заблокирован, разблокируйте и нажмите «Доверять»");
    } else {
      SetState(ConnState::NoDevice, "Подключите iPhone кабелем");
    }
    return false;
  }
  Attach(std::move(*socket), Transport::Usb);
  return true;
}

void ConnectionManager::TryWifi(bool usbAvailable) {
  const auto phones = WifiPhones();
  std::optional<WifiPhone> target;
  std::optional<std::string> code;
  {
    std::lock_guard lock(mutex_);
    if (selectedWifi_) {
      auto it = std::ranges::find(phones, *selectedWifi_, &WifiPhone::id);
      if (it != phones.end()) target = *it;
    }
    if (!target && phones.size() == 1) target = phones[0];
    if (target) {
      auto known = wifiCodes_.find(target->id);
      if (known != wifiCodes_.end()) code = known->second;
    }
  }
  if (!target) {
    // With a phone on the cable its message is the more useful one.
    if (phones.size() > 1) {
      SetState(ConnState::ChooseDevice, "В сети несколько iPhone, выберите нужный");
    } else if (!usbAvailable) {
      SetState(ConnState::NoDevice,
               "Ищем iPhone в сети… Откройте на нём Webicam; компьютер и iPhone должны быть в одной сети");
    }
    return;
  }
  if (!code) {
    std::lock_guard lock(mutex_);
    codeRequest_ = target;
    state_ = ConnState::WifiNeedsCode;
    detail_ = "Введите код с экрана iPhone «" + target->name + "»";
    return;
  }
  auto socket = Socket::ConnectTcp(target->host.c_str(), target->port, kWifiConnectTimeoutMs);
  if (!socket) {
    if (target->id.rfind("addr:", 0) != 0) browser_.Refresh(target->id);
    SetState(ConnState::PhoneAppNotOpen,
             "iPhone «" + target->name + "» не отвечает по Wi-Fi. Откройте на нём Webicam");
    return;
  }
  Attach(std::move(*socket), Transport::Wifi, std::move(target), std::move(code));
}

void ConnectionManager::TryConnect() {
  if (direct_) {
    auto socket = Socket::ConnectTcp(direct_->first.c_str(), direct_->second, 500);
    if (!socket) {
      SetState(ConnState::PhoneAppNotOpen,
               "Нет ответа от " + direct_->first + ":" + std::to_string(direct_->second) + " (режим --direct)");
      return;
    }
    Attach(std::move(*socket), Transport::Direct);
    return;
  }
  const LinkMode mode = GetLinkMode();
  if (mode == LinkMode::Wifi) {
    TryWifi(false);
    return;
  }
  if (TryUsb(false) || mode == LinkMode::Usb) return;
  ConnState usbState;
  {
    std::lock_guard lock(mutex_);
    usbState = state_;
  }
  // A phone on the cable that only needs the app opened (or picking) keeps its message.
  const bool usbAvailable = usbState == ConnState::PhoneAppNotOpen || usbState == ConnState::ChooseDevice ||
                            usbState == ConnState::AppleServiceMissing;
  if (usbState == ConnState::AppleServiceMissing) {
    SetState(ConnState::AppleServiceMissing,
             "Для USB установите «Apple Devices» из Microsoft Store или откройте Webicam на iPhone в той же Wi-Fi сети");
  } else if (!usbAvailable) {
    SetState(ConnState::NoDevice, "Подключите iPhone кабелем или откройте на нём Webicam в той же Wi-Fi сети");
  }
  TryWifi(true);
}

void ConnectionManager::PreferUsb() {
  {
    std::lock_guard lock(mutex_);
    if (!session_ || transport_ != Transport::Wifi || linkMode_ != LinkMode::Auto) return;
    const uint64_t now = SteadyNowMs();
    if (now - lastUsbProbeMs_ < kUsbProbeMs) return;
    lastUsbProbeMs_ = now;
  }
  auto devices = usbmux_.ListDevices();
  if (!devices || devices->size() != 1) return;
  auto socket = usbmux_.Connect((*devices)[0].deviceId, proto::kPhonePort);
  if (!socket) return;
  std::unique_ptr<DeviceSession> wifi;
  {
    std::lock_guard lock(mutex_);
    wifi = std::move(session_);
    devices_ = *devices;
  }
  DropSession(std::move(wifi));
  Attach(std::move(*socket), Transport::Usb);
}

void ConnectionManager::DropSession(std::unique_ptr<DeviceSession> dead) {
  bool wasStreaming;
  {
    std::lock_guard lock(mutex_);
    wasStreaming = streaming_;
    streaming_ = false;
    transport_ = Transport::None;
    telemetry_.reset();
  }
  dead.reset();  // joins the session thread; safe here, never on that thread
  if (wasStreaming && callbacks_.onConnectionChanged) callbacks_.onConnectionChanged(false);
}

void ConnectionManager::Run() {
  uint64_t lastAttemptMs = 0;
  while (!stop_) {
    std::unique_ptr<DeviceSession> dead;
    std::string reason;
    std::string rejected;
    std::optional<WifiPhone> peer;
    {
      std::lock_guard lock(mutex_);
      if (session_ && session_->Closed()) {
        dead = std::move(session_);
        reason = closeReason_;
        rejected = pairingRejected_;
        peer = wifiPeer_;
      }
    }
    if (dead) {
      DropSession(std::move(dead));
      if (!rejected.empty() && peer) {
        std::lock_guard lock(mutex_);
        codeRequest_ = peer;
        state_ = ConnState::WifiNeedsCode;
        detail_ = rejected;
      } else if (reason.find("версия") != std::string::npos) {
        SetState(ConnState::VersionMismatch, "Версии приложений не совпадают: обновите Webicam (" + reason + ")");
      } else {
        SetState(ConnState::Connecting, "Соединение потеряно, переподключение…");
      }
      lastAttemptMs = 0;
    }

    bool haveSession;
    ConnState state;
    bool retryNow;
    {
      std::lock_guard lock(mutex_);
      haveSession = session_ != nullptr;
      state = state_;
      retryNow = std::exchange(retryNow_, false);
    }
    const uint64_t now = SteadyNowMs();
    const uint64_t retry = state == ConnState::VersionMismatch ? 5000 : kRetryMs;
    // Waiting for a code keeps its message until the user types it (SetWifiCode sets retryNow_);
    // meanwhile only a cable, if allowed, can take over.
    const bool waiting = state == ConnState::WifiNeedsCode;
    if (!haveSession && (retryNow || now - lastAttemptMs >= retry)) {
      lastAttemptMs = now;
      if (!waiting || retryNow) {
        TryConnect();
      } else if (GetLinkMode() != LinkMode::Wifi) {
        TryUsb(true);
      }
    } else if (haveSession) {
      PreferUsb();
    }
    std::this_thread::sleep_for(kPollInterval);
  }
}

}  // namespace webicam
