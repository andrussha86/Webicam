#include "webicam/usbmux.h"

#include <array>

#include "webicam/plist.h"

namespace webicam {

namespace {

constexpr int kTimeoutMs = 1500;
constexpr uint32_t kVersionPlist = 1;
constexpr uint32_t kMessagePlist = 8;

void PutU32(std::vector<uint8_t>& out, uint32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i)));
}

uint32_t GetU32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

PlistValue Request(const char* messageType, std::vector<std::pair<std::string, PlistValue>> extra = {}) {
  std::vector<std::pair<std::string, PlistValue>> entries{
      {"MessageType", PlistValue::Str(messageType)},
      {"ClientVersionString", PlistValue::Str("webicam")},
      {"ProgName", PlistValue::Str("webicam")},
  };
  for (auto& e : extra) entries.push_back(std::move(e));
  return PlistValue::Dict(std::move(entries));
}

bool SendPlist(Socket& socket, const PlistValue& message, uint32_t tag) {
  const std::string xml = ToXml(message);
  std::vector<uint8_t> packet;
  PutU32(packet, uint32_t(16 + xml.size()));
  PutU32(packet, kVersionPlist);
  PutU32(packet, kMessagePlist);
  PutU32(packet, tag);
  packet.insert(packet.end(), xml.begin(), xml.end());
  return socket.SendAll(packet);
}

std::optional<PlistValue> ReceivePlist(Socket& socket) {
  std::array<uint8_t, 16> header{};
  if (!socket.RecvAll(header, kTimeoutMs)) return std::nullopt;
  const uint32_t length = GetU32(header.data());
  if (length < 16 || length > (1u << 20)) return std::nullopt;
  std::string body(length - 16, '\0');
  if (!socket.RecvAll(std::span(reinterpret_cast<uint8_t*>(body.data()), body.size()), kTimeoutMs)) {
    return std::nullopt;
  }
  return ParseXml(body);
}

std::expected<Socket, UsbmuxError> OpenService(uint16_t port) {
  auto socket = Socket::ConnectTcp("127.0.0.1", port, kTimeoutMs);
  if (!socket) return std::unexpected(UsbmuxError::ServiceUnavailable);
  return std::move(*socket);
}

}  // namespace

std::expected<std::vector<UsbmuxDevice>, UsbmuxError> UsbmuxClient::ListDevices() {
  auto socket = OpenService(port_);
  if (!socket) return std::unexpected(socket.error());
  if (!SendPlist(*socket, Request("ListDevices"), tag_++)) return std::unexpected(UsbmuxError::Protocol);
  auto reply = ReceivePlist(*socket);
  if (!reply) return std::unexpected(UsbmuxError::Protocol);
  const PlistValue* list = reply->Find("DeviceList");
  if (!list || list->kind != PlistValue::Kind::Array) return std::unexpected(UsbmuxError::Protocol);

  std::vector<UsbmuxDevice> devices;
  for (const auto& entry : list->array) {
    const PlistValue* properties = entry.Find("Properties");
    if (!properties) continue;
    const PlistValue* id = properties->Find("DeviceID");
    const PlistValue* type = properties->Find("ConnectionType");
    const PlistValue* serial = properties->Find("SerialNumber");
    if (!id || !type || type->str != "USB") continue;
    devices.push_back({uint32_t(id->integer), serial ? serial->str : std::string{}, type->str});
  }
  return devices;
}

std::expected<Socket, UsbmuxError> UsbmuxClient::Connect(uint32_t deviceId, uint16_t devicePort) {
  auto socket = OpenService(port_);
  if (!socket) return std::unexpected(socket.error());
  // usbmuxd expects the port in network byte order, stored as an integer.
  const uint16_t networkPort = uint16_t((devicePort >> 8) | (devicePort << 8));
  auto request = Request("Connect", {{"DeviceID", PlistValue::Int(deviceId)}, {"PortNumber", PlistValue::Int(networkPort)}});
  if (!SendPlist(*socket, request, tag_++)) return std::unexpected(UsbmuxError::Protocol);
  auto reply = ReceivePlist(*socket);
  if (!reply) return std::unexpected(UsbmuxError::Protocol);
  const PlistValue* number = reply->Find("Number");
  if (!number) return std::unexpected(UsbmuxError::Protocol);
  switch (number->integer) {
    case 0: return std::move(*socket);
    case 2: return std::unexpected(UsbmuxError::DeviceNotFound);
    case 3: return std::unexpected(UsbmuxError::ConnectionRefused);
    default: return std::unexpected(UsbmuxError::Protocol);
  }
}

}  // namespace webicam
