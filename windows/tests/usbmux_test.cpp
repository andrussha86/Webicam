#include <gtest/gtest.h>

#include <array>
#include <chrono>

#include "fake_usbmuxd.h"
#include "webicam/plist.h"
#include "webicam/usbmux.h"

using namespace webicam;

TEST(Plist, RoundTrip) {
  auto value = PlistValue::Dict({
      {"MessageType", PlistValue::Str("Connect")},
      {"DeviceID", PlistValue::Int(7)},
      {"Nested", PlistValue::Dict({{"A", PlistValue::Str("x & <y>")}})},
      {"List", PlistValue::Array({PlistValue::Int(1), PlistValue::Str("two")})},
  });
  auto parsed = ParseXml(ToXml(value));
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(*parsed, value);
}

TEST(Plist, ParsesRealListDevicesReply) {
  const char* xml =
      R"(<?xml version="1.0" encoding="UTF-8"?><!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd"><plist version="1.0"><dict><key>DeviceList</key><array><dict><key>DeviceID</key><integer>3</integer><key>MessageType</key><string>Attached</string><key>Properties</key><dict><key>ConnectionType</key><string>USB</string><key>DeviceID</key><integer>3</integer><key>SerialNumber</key><string>00008130-001A2B3C4D5E6F7A</string></dict></dict></array></dict></plist>)";
  auto parsed = ParseXml(xml);
  ASSERT_TRUE(parsed.has_value());
  const auto& device = parsed->Find("DeviceList")->array.at(0);
  EXPECT_EQ(device.Find("DeviceID")->integer, 3);
  EXPECT_EQ(device.Find("Properties")->Find("SerialNumber")->str, "00008130-001A2B3C4D5E6F7A");
  EXPECT_EQ(device.Find("Properties")->Find("ConnectionType")->str, "USB");
}

TEST(Plist, GarbageIsNullopt) { EXPECT_FALSE(ParseXml("not a plist <dict>").has_value()); }

TEST(Usbmux, ListDevicesParsesUsbOnly) {
  FakeUsbmuxd fake([](const PlistValue&) { return DeviceListReply({{3, "USB"}, {4, "Network"}}); });
  auto devices = UsbmuxClient(fake.port()).ListDevices();
  ASSERT_TRUE(devices.has_value());
  ASSERT_EQ(devices->size(), 1u);
  EXPECT_EQ((*devices)[0].deviceId, 3u);
  EXPECT_EQ((*devices)[0].serial, "SERIAL3");
}

TEST(Usbmux, ConnectSendsBigEndianPortAndTunnels) {
  FakeUsbmuxd fake([](const PlistValue&) { return ResultReply(0); });
  auto socket = UsbmuxClient(fake.port()).Connect(3, 47800);
  ASSERT_TRUE(socket.has_value());
  const auto request = fake.lastRequest();
  EXPECT_EQ(request.Find("PortNumber")->integer, 0xB8BA);
  EXPECT_EQ(request.Find("DeviceID")->integer, 3);

  const std::array<uint8_t, 4> ping{'p', 'i', 'n', 'g'};
  ASSERT_TRUE(socket->SendAll(ping));
  std::array<uint8_t, 4> echo{};
  ASSERT_TRUE(socket->RecvAll(echo, 1000));
  EXPECT_EQ(echo, ping);
}

TEST(Usbmux, ConnectRefusedMapsError) {
  FakeUsbmuxd fake([](const PlistValue&) { return ResultReply(3); });
  auto socket = UsbmuxClient(fake.port()).Connect(3, 47800);
  ASSERT_FALSE(socket.has_value());
  EXPECT_EQ(socket.error(), UsbmuxError::ConnectionRefused);
}

TEST(Usbmux, ServiceDownGivesServiceUnavailable) {
  uint16_t port;
  {
    auto listener = Socket::Listen("127.0.0.1", 0);
    port = listener->LocalPort();
  }
  const auto start = std::chrono::steady_clock::now();
  auto devices = UsbmuxClient(port).ListDevices();
  ASSERT_FALSE(devices.has_value());
  EXPECT_EQ(devices.error(), UsbmuxError::ServiceUnavailable);
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(2));
}
