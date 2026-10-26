#include "webicam/device_session.h"

#include <windows.h>

#include <array>
#include <vector>

namespace webicam {

uint64_t NowUs() {
  static const LONGLONG frequency = [] {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f.QuadPart;
  }();
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return uint64_t(now.QuadPart / frequency * 1'000'000 + now.QuadPart % frequency * 1'000'000 / frequency);
}

DeviceSession::DeviceSession(Socket socket, Events events, int idleTimeoutMs)
    : socket_(std::move(socket)), events_(std::move(events)), idleTimeoutMs_(idleTimeoutMs) {}

DeviceSession::~DeviceSession() {
  stopping_ = true;
  socket_.Shutdown();
  if (thread_.joinable()) thread_.join();
}

void DeviceSession::Start() {
  Send(proto::Hello{proto::kProtocolVersion, {}, {}});
  thread_ = std::thread([this] { Run(); });
}

void DeviceSession::SendConfigure(const proto::Configure& configure) { Send(configure); }

void DeviceSession::RequestKeyframe() { Send(proto::RequestKeyframe{}); }

std::optional<int64_t> DeviceSession::PhoneToPcUs(uint64_t phoneUs) const {
  std::lock_guard lock(clockMutex_);
  const auto offset = clock_.OffsetUs();
  if (!offset) return std::nullopt;
  return int64_t(phoneUs) + *offset;
}

bool DeviceSession::Send(const proto::Message& message) {
  const auto bytes = proto::Encode(message);
  std::lock_guard lock(sendMutex_);
  return socket_.SendAll(bytes);
}

void DeviceSession::Close(std::string reason) {
  if (closed_.exchange(true)) return;
  socket_.Shutdown();
  if (!stopping_ && events_.onClosed) events_.onClosed(std::move(reason));
}

void DeviceSession::Run() {
  proto::MessageParser parser;
  std::vector<uint8_t> buffer(256 * 1024);
  bool handshaken = false;
  uint64_t lastPingUs = 0;
  uint64_t lastReceiveUs = NowUs();

  while (!stopping_ && !closed_) {
    const uint64_t now = NowUs();
    if (handshaken && now - lastPingUs >= 1'000'000) {
      lastPingUs = now;
      Send(proto::Ping{now});
    }

    if (now - lastReceiveUs > uint64_t(idleTimeoutMs_) * 1000) {
      Close("нет данных от телефона");
      return;
    }
    const int received = socket_.Recv(buffer, 100);
    if (received == -2) continue;
    if (received <= 0) {
      Close(received == 0 ? "соединение закрыто телефоном" : "ошибка соединения");
      return;
    }
    lastReceiveUs = NowUs();
    parser.Feed(std::span(buffer.data(), size_t(received)));

    proto::Message message;
    proto::MessageParser::Result result;
    while ((result = parser.Next(message)) == proto::MessageParser::Result::Message) {
      if (!handshaken) {
        const auto* hello = std::get_if<proto::Hello>(&message);
        if (!hello) {
          Close("ошибка протокола: первым сообщением ожидался Hello");
          return;
        }
        if (hello->version != proto::kProtocolVersion) {
          Close("версия протокола не совпадает (телефон: " + std::to_string(hello->version) +
                ", ПК: " + std::to_string(proto::kProtocolVersion) + ")");
          return;
        }
        handshaken = true;
        if (events_.onHello) events_.onHello(*hello);
        continue;
      }
      if (auto* frame = std::get_if<proto::VideoFrame>(&message)) {
        if (events_.onFrame) events_.onFrame(std::move(*frame));
      } else if (auto* pong = std::get_if<proto::Pong>(&message)) {
        std::lock_guard lock(clockMutex_);
        clock_.AddSample(pong->pcTimeUs, pong->phoneTimeUs, NowUs());
      } else if (auto* status = std::get_if<proto::Status>(&message)) {
        if (events_.onStatus) events_.onStatus(*status);
      }
    }
    if (result == proto::MessageParser::Result::Error) {
      Close("ошибка протокола");
      return;
    }
  }
}

}  // namespace webicam
