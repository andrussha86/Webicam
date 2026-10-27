#include "webicam/connection_manager.h"

#include "webicam/vcam_logic.h"

#include <algorithm>
#include <chrono>

namespace webicam {

namespace {

constexpr auto kPollInterval = std::chrono::milliseconds(250);
constexpr uint64_t kRetryMs = 1500;

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

}  // namespace

ConnectionManager::ConnectionManager(Callbacks callbacks, uint16_t usbmuxPort)
    : callbacks_(std::move(callbacks)), usbmux_(usbmuxPort) {}

ConnectionManager::~ConnectionManager() { Stop(); }

void ConnectionManager::Stop() {
  stop_ = true;
  if (thread_.joinable()) thread_.join();
  std::unique_ptr<DeviceSession> session;
  {
    std::lock_guard lock(mutex_);
    session = std::move(session_);
  }
  session.reset();
}

void ConnectionManager::SetDirect(std::string host, uint16_t port) { direct_ = {std::move(host), port}; }

void ConnectionManager::Start() { thread_ = std::thread([this] { Run(); }); }

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

void ConnectionManager::SetState(ConnState state, std::string detail) {
  std::lock_guard lock(mutex_);
  state_ = state;
  detail_ = std::move(detail);
}

void ConnectionManager::Attach(Socket socket) {
  SetState(ConnState::Connecting, "Подключение…");
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
      detail_ = "Подключено: " + (hello.model.empty() ? std::string("iPhone") : hello.model);
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
  events.onClosed = [this](std::string reason) {
    std::lock_guard lock(mutex_);
    closeReason_ = std::move(reason);
  };
  auto session = std::make_unique<DeviceSession>(std::move(socket), std::move(events));
  DeviceSession* raw = session.get();
  {
    std::lock_guard lock(mutex_);
    session_ = std::move(session);
    phoneCamera_.reset();
    closeReason_.clear();
  }
  raw->Start();
}

void ConnectionManager::TryConnect() {
  if (direct_) {
    auto socket = Socket::ConnectTcp(direct_->first.c_str(), direct_->second, 500);
    if (!socket) {
      SetState(ConnState::PhoneAppNotOpen,
               "Нет ответа от " + direct_->first + ":" + std::to_string(direct_->second) + " (режим --direct)");
      return;
    }
    Attach(std::move(*socket));
    return;
  }

  auto devices = usbmux_.ListDevices();
  if (!devices) {
    if (devices.error() == UsbmuxError::ServiceUnavailable) {
      SetState(ConnState::AppleServiceMissing, "Установите «Apple Devices» из Microsoft Store");
    } else {
      SetState(ConnState::NoDevice, "Служба Apple Mobile Device ответила с ошибкой");
    }
    return;
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
    SetState(ConnState::NoDevice, "Подключите iPhone кабелем");
    return;
  }
  if (!target) {
    SetState(ConnState::ChooseDevice, "Подключено несколько iPhone, выберите нужный");
    return;
  }
  auto socket = usbmux_.Connect(*target, proto::kPhonePort);
  if (!socket) {
    if (socket.error() == UsbmuxError::ConnectionRefused) {
      SetState(ConnState::PhoneAppNotOpen,
               "Откройте Webicam на iPhone. Если он заблокирован, разблокируйте и нажмите «Доверять»");
    } else {
      SetState(ConnState::NoDevice, "Подключите iPhone кабелем");
    }
    return;
  }
  Attach(std::move(*socket));
}

void ConnectionManager::Run() {
  uint64_t lastAttemptMs = 0;
  while (!stop_) {
    std::unique_ptr<DeviceSession> dead;
    bool wasStreaming = false;
    std::string reason;
    {
      std::lock_guard lock(mutex_);
      if (session_ && session_->Closed()) {
        dead = std::move(session_);
        wasStreaming = streaming_;
        streaming_ = false;
        reason = closeReason_;
      }
    }
    if (dead) {
      dead.reset();  // joins the session thread; safe here, never on that thread
      if (wasStreaming && callbacks_.onConnectionChanged) callbacks_.onConnectionChanged(false);
      if (reason.find("версия") != std::string::npos) {
        SetState(ConnState::VersionMismatch, "Версии приложений не совпадают: обновите Webicam (" + reason + ")");
      } else {
        SetState(ConnState::Connecting, "Соединение потеряно, переподключение…");
      }
      lastAttemptMs = 0;
    }

    bool haveSession;
    ConnState state;
    {
      std::lock_guard lock(mutex_);
      haveSession = session_ != nullptr;
      state = state_;
    }
    const uint64_t now = SteadyNowMs();
    const uint64_t retry = state == ConnState::VersionMismatch ? 5000 : kRetryMs;
    if (!haveSession && now - lastAttemptMs >= retry) {
      lastAttemptMs = now;
      TryConnect();
    }
    std::this_thread::sleep_for(kPollInterval);
  }
}

}  // namespace webicam
