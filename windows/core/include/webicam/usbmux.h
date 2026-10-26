#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include "webicam/net.h"

namespace webicam {

struct UsbmuxDevice {
  uint32_t deviceId;
  std::string serial;
  std::string connectionType;
  bool operator==(const UsbmuxDevice&) const = default;
};

enum class UsbmuxError { ServiceUnavailable, DeviceNotFound, ConnectionRefused, Protocol };

// Talks to Apple Mobile Device Service (usbmuxd) on 127.0.0.1:servicePort.
class UsbmuxClient {
 public:
  static constexpr uint16_t kDefaultPort = 27015;

  explicit UsbmuxClient(uint16_t servicePort = kDefaultPort) : port_(servicePort) {}

  // USB-connected devices only.
  std::expected<std::vector<UsbmuxDevice>, UsbmuxError> ListDevices();
  // On success the returned socket is a raw tunnel to devicePort on the phone.
  std::expected<Socket, UsbmuxError> Connect(uint32_t deviceId, uint16_t devicePort);

 private:
  uint16_t port_;
  uint32_t tag_ = 1;
};

}  // namespace webicam
