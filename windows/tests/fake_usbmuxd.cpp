#include "fake_usbmuxd.h"

#include <array>
#include <mutex>
#include <string>

using namespace webicam;

namespace {

uint32_t GetU32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

void PutU32(std::vector<uint8_t>& out, uint32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i)));
}

}  // namespace

FakeUsbmuxd::FakeUsbmuxd(Handler handler) : handler_(std::move(handler)) {
  listener_ = std::move(*Socket::Listen("127.0.0.1", 0));
  port_ = listener_.LocalPort();
  thread_ = std::thread([this] { Run(); });
}

FakeUsbmuxd::~FakeUsbmuxd() {
  stop_ = true;
  thread_.join();
}

PlistValue FakeUsbmuxd::lastRequest() const {
  std::lock_guard lock(mutex_);
  return lastRequest_;
}

void FakeUsbmuxd::Run() {
  while (!stop_) {
    Socket client = listener_.Accept(50);
    if (!client.valid()) continue;
    std::array<uint8_t, 16> header{};
    if (!client.RecvAll(header, 1000)) continue;
    std::string body(GetU32(header.data()) - 16, '\0');
    if (!client.RecvAll(std::span(reinterpret_cast<uint8_t*>(body.data()), body.size()), 1000)) continue;
    auto request = ParseXml(body);
    if (!request) continue;
    {
      std::lock_guard lock(mutex_);
      lastRequest_ = *request;
    }
    const PlistValue reply = handler_(*request);
    const std::string xml = ToXml(reply);
    std::vector<uint8_t> packet;
    PutU32(packet, uint32_t(16 + xml.size()));
    PutU32(packet, 1);
    PutU32(packet, 8);
    PutU32(packet, GetU32(header.data() + 12));
    packet.insert(packet.end(), xml.begin(), xml.end());
    client.SendAll(packet);

    const PlistValue* type = request->Find("MessageType");
    const PlistValue* number = reply.Find("Number");
    if (type && type->str == "Connect" && number && number->integer == 0) {
      std::array<uint8_t, 256> buffer{};
      while (!stop_) {
        const int r = client.Recv(buffer, 50);
        if (r == -2) continue;
        if (r <= 0) break;
        client.SendAll(std::span(buffer).first(size_t(r)));
      }
    }
  }
}

PlistValue ResultReply(int64_t number) {
  return PlistValue::Dict({{"MessageType", PlistValue::Str("Result")}, {"Number", PlistValue::Int(number)}});
}

PlistValue DeviceListReply(std::vector<std::pair<int64_t, std::string>> idsAndTypes) {
  std::vector<PlistValue> list;
  for (const auto& [id, type] : idsAndTypes) {
    list.push_back(PlistValue::Dict({
        {"DeviceID", PlistValue::Int(id)},
        {"MessageType", PlistValue::Str("Attached")},
        {"Properties", PlistValue::Dict({{"ConnectionType", PlistValue::Str(type)},
                                         {"DeviceID", PlistValue::Int(id)},
                                         {"SerialNumber", PlistValue::Str("SERIAL" + std::to_string(id))}})},
    }));
  }
  return PlistValue::Dict({{"DeviceList", PlistValue::Array(std::move(list))}});
}
