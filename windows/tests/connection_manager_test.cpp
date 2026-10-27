#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include "fake_usbmuxd.h"
#include "webicam/connection_manager.h"
#include "webicam/single_instance.h"
#include "webicam/testvectors.h"

using namespace webicam;
using namespace std::chrono_literals;

namespace {

template <typename Pred>
bool WaitFor(Pred pred, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) return true;
    std::this_thread::sleep_for(20ms);
  }
  return pred();
}

uint16_t UnusedPort() {
  auto listener = Socket::Listen("127.0.0.1", 0);
  return listener->LocalPort();
}

// A phone that says Hello and records every message the PC sends.
class RecordingPhone {
 public:
  RecordingPhone() : listener_(*Socket::Listen("127.0.0.1", 0)) {
    thread_ = std::thread([this] {
      Socket client;
      while (!stop_ && !client.valid()) client = listener_.Accept(50);
      if (!client.valid()) return;
      client.SendAll(proto::Encode(proto::TestVectors()[1].message));
      proto::MessageParser parser;
      std::vector<uint8_t> buffer(1 << 16);
      while (!stop_) {
        const int n = client.Recv(buffer, 50);
        if (n < 0) return;
        if (n == 0) continue;
        parser.Feed(std::span(buffer.data(), size_t(n)));
        proto::Message message;
        while (parser.Next(message) == proto::MessageParser::Result::Message) {
          std::lock_guard lock(mutex_);
          messages_.push_back(std::move(message));
        }
      }
    });
  }
  ~RecordingPhone() {
    stop_ = true;
    thread_.join();
  }
  uint16_t Port() const { return listener_.LocalPort(); }
  std::vector<proto::Message> Messages() const {
    std::lock_guard lock(mutex_);
    return messages_;
  }
  size_t Count(size_t variantIndex) const {
    size_t n = 0;
    for (const auto& m : Messages()) n += m.index() == variantIndex;
    return n;
  }

 private:
  Socket listener_;
  std::atomic<bool> stop_{false};
  mutable std::mutex mutex_;
  std::vector<proto::Message> messages_;
  std::thread thread_;
};

constexpr size_t kConfigureIndex = 1, kCameraSettingsIndex = 7, kBackgroundImageIndex = 9;

}  // namespace

TEST(ConnectionManager, SendsBackgroundBeforeSettings) {
  RecordingPhone phone;
  std::atomic<uint32_t> requested{0};
  ConnectionManager manager({});
  manager.SetBackgroundSource([&](uint16_t w, uint16_t h) {
    requested = uint32_t(w) << 16 | h;
    return std::vector<uint8_t>{1, 2, 3};
  });
  proto::CameraSettings camera;
  camera.backgroundMode = proto::BackgroundMode::Image;
  manager.SetDesiredCamera(camera);
  manager.SetDirect("127.0.0.1", phone.Port());
  manager.Start();
  ASSERT_TRUE(WaitFor([&] { return phone.Count(kCameraSettingsIndex) >= 1; }, 5000ms));
  const auto messages = phone.Messages();
  std::vector<size_t> types;
  for (const auto& m : messages) {
    if (m.index() != 0) types.push_back(m.index());  // skip the PC's Hello
  }
  ASSERT_GE(types.size(), 3u);
  EXPECT_EQ(types[0], kConfigureIndex);
  EXPECT_EQ(types[1], kBackgroundImageIndex);
  EXPECT_EQ(types[2], kCameraSettingsIndex);
  const auto& configure = std::get<proto::Configure>(messages[1]);
  EXPECT_EQ(requested.load(), uint32_t(configure.width) << 16 | configure.height);
  EXPECT_EQ(std::get<proto::BackgroundImage>(messages[2]).jpeg, (std::vector<uint8_t>{1, 2, 3}));
  manager.Stop();
}

TEST(ConnectionManager, NoBackgroundWhenModeOff) {
  RecordingPhone phone;
  ConnectionManager manager({});
  manager.SetBackgroundSource([](uint16_t, uint16_t) { return std::vector<uint8_t>{1}; });
  manager.SetDirect("127.0.0.1", phone.Port());
  manager.Start();
  ASSERT_TRUE(WaitFor([&] { return phone.Count(kCameraSettingsIndex) >= 1; }, 5000ms));
  EXPECT_EQ(phone.Count(kBackgroundImageIndex), 0u);
  manager.Stop();
}

TEST(ConnectionManager, InvalidateResends) {
  RecordingPhone phone;
  ConnectionManager manager({});
  manager.SetBackgroundSource([](uint16_t, uint16_t) { return std::vector<uint8_t>{9}; });
  proto::CameraSettings camera;
  camera.backgroundMode = proto::BackgroundMode::Image;
  manager.SetDesiredCamera(camera);
  manager.SetDirect("127.0.0.1", phone.Port());
  manager.Start();
  ASSERT_TRUE(WaitFor([&] { return phone.Count(kBackgroundImageIndex) == 1; }, 5000ms));
  manager.InvalidateBackground();
  EXPECT_TRUE(WaitFor([&] { return phone.Count(kBackgroundImageIndex) == 2; }, 2000ms));
  manager.Stop();
}

TEST(SingleInstance, SecondAcquireFails) {
  const std::wstring name = L"Local\\WebicamTest-Single-" + std::to_wstring(GetCurrentProcessId());
  auto first = AcquireSingleInstance(name.c_str());
  ASSERT_TRUE(first.has_value());
  EXPECT_FALSE(AcquireSingleInstance(name.c_str()).has_value());
  CloseHandle(*first);
}

TEST(ConnectionManager, ServiceMissingState) {
  ConnectionManager manager({}, UnusedPort());
  manager.Start();
  EXPECT_TRUE(WaitFor([&] { return manager.State() == ConnState::AppleServiceMissing; }, 2000ms));
  EXPECT_NE(manager.Detail().find("Apple Devices"), std::string::npos);
}

TEST(ConnectionManager, RefusedMeansPhoneAppNotOpenAndAutoSelectsSingleDevice) {
  FakeUsbmuxd fake([](const PlistValue& request) {
    const auto* type = request.Find("MessageType");
    if (type && type->str == "ListDevices") return DeviceListReply({{3, "USB"}});
    return ResultReply(3);
  });
  ConnectionManager manager({}, fake.port());
  manager.Start();
  EXPECT_TRUE(WaitFor([&] { return manager.State() == ConnState::PhoneAppNotOpen; }, 3000ms));
  EXPECT_EQ(manager.Devices().size(), 1u);
  EXPECT_EQ(fake.lastRequest().Find("DeviceID")->integer, 3);
}

TEST(ConnectionManager, TwoDevicesAskToChoose) {
  FakeUsbmuxd fake([](const PlistValue&) { return DeviceListReply({{3, "USB"}, {5, "USB"}}); });
  ConnectionManager manager({}, fake.port());
  manager.Start();
  EXPECT_TRUE(WaitFor([&] { return manager.State() == ConnState::ChooseDevice; }, 3000ms));
}

TEST(ConnectionManager, ReconnectsAfterClose) {
  auto listener = Socket::Listen("127.0.0.1", 0);
  const uint16_t port = listener->LocalPort();
  std::atomic<int> connections{0};
  std::atomic<int> changes{0};
  std::atomic<bool> stop{false};
  std::thread phone([&] {
    while (!stop) {
      Socket client = listener->Accept(50);
      if (!client.valid()) continue;
      ++connections;
      client.SendAll(proto::Encode(proto::TestVectors()[1].message));
      std::this_thread::sleep_for(300ms);
      if (connections == 1) continue;  // first connection: drop it
      while (!stop) std::this_thread::sleep_for(20ms);
    }
  });

  ConnectionManager manager({.onConnectionChanged = [&](bool) { ++changes; }});
  manager.SetDirect("127.0.0.1", port);
  manager.Start();
  EXPECT_TRUE(WaitFor([&] { return connections >= 2 && manager.State() == ConnState::Streaming; }, 5000ms));
  EXPECT_GE(changes.load(), 3);  // up, down, up
  stop = true;
  phone.join();
}

namespace {

// A Wi-Fi phone: waits for the PC's Hello and Pair, then says Hello and sends Telemetry when the
// code matches, or refuses the code and closes.
class PairingPhone {
 public:
  explicit PairingPhone(std::string code) : code_(std::move(code)), listener_(*Socket::Listen("127.0.0.1", 0)) {
    thread_ = std::thread([this] {
      while (!stop_) {
        Socket client = listener_.Accept(50);
        if (!client.valid()) continue;
        Serve(client);
      }
    });
  }
  ~PairingPhone() {
    stop_ = true;
    thread_.join();
  }
  uint16_t Port() const { return listener_.LocalPort(); }
  std::string Address() const { return "127.0.0.1:" + std::to_string(Port()); }

 private:
  void Serve(Socket& client) {
    proto::MessageParser parser;
    std::vector<uint8_t> buffer(4096);
    while (!stop_) {
      const int n = client.Recv(buffer, 50);
      if (n == -2) continue;
      if (n <= 0) return;
      parser.Feed(std::span(buffer.data(), size_t(n)));
      proto::Message message;
      while (parser.Next(message) == proto::MessageParser::Result::Message) {
        const auto* pair = std::get_if<proto::Pair>(&message);
        if (!pair) continue;
        if (pair->code != code_) {
          client.SendAll(proto::Encode(proto::Status{proto::StreamState::PairingRejected, 0, "Неверный код"}));
          return;
        }
        client.SendAll(proto::Encode(proto::TestVectors()[1].message));
        client.SendAll(proto::Encode(proto::Telemetry{64, proto::Charging::Unplugged, 1}));
        while (!stop_ && client.Recv(buffer, 50) != 0) {
        }
        return;
      }
    }
  }

  std::string code_;
  Socket listener_;
  std::atomic<bool> stop_{false};
  std::thread thread_;
};

}  // namespace

TEST(ConnectionManager, WifiAsksForCodeThenStreams) {
  PairingPhone phone("123456");
  ConnectionManager manager({}, UnusedPort());
  manager.SetLinkMode(LinkMode::Wifi);
  manager.SetWifiAddress(phone.Address());
  manager.Start();
  ASSERT_TRUE(WaitFor([&] { return manager.State() == ConnState::WifiNeedsCode; }, 3000ms));
  const auto request = manager.CodeRequest();
  ASSERT_TRUE(request.has_value());
  EXPECT_EQ(request->id, "addr:" + phone.Address());

  manager.SetWifiCode(request->id, "123456");
  ASSERT_TRUE(WaitFor([&] { return manager.State() == ConnState::Streaming; }, 3000ms));
  EXPECT_EQ(manager.CurrentTransport(), Transport::Wifi);
  ASSERT_TRUE(WaitFor([&] { return manager.PhoneTelemetry().has_value(); }, 2000ms));
  EXPECT_EQ(manager.PhoneTelemetry()->battery, 64);
  EXPECT_EQ(manager.WifiCodes().at(request->id), "123456");
  manager.Stop();
}

TEST(ConnectionManager, WrongWifiCodeIsForgottenAndAskedAgain) {
  PairingPhone phone("123456");
  ConnectionManager manager({}, UnusedPort());
  manager.SetLinkMode(LinkMode::Wifi);
  manager.SetWifiAddress(phone.Address());
  const std::string id = "addr:" + phone.Address();
  manager.SetWifiCodes({{id, "000000"}});
  manager.Start();
  ASSERT_TRUE(WaitFor([&] { return manager.State() == ConnState::WifiNeedsCode; }, 3000ms));
  EXPECT_NE(manager.Detail().find("Неверный код"), std::string::npos);
  EXPECT_EQ(manager.WifiCodes().count(id), 0u);
  // Stays asking instead of retrying with no code.
  std::this_thread::sleep_for(2000ms);
  EXPECT_EQ(manager.State(), ConnState::WifiNeedsCode);
  manager.Stop();
}

TEST(ConnectionManager, UsbModeIgnoresWifiAddress) {
  PairingPhone phone("123456");
  ConnectionManager manager({}, UnusedPort());
  manager.SetLinkMode(LinkMode::Usb);
  manager.SetWifiAddress(phone.Address());
  manager.Start();
  EXPECT_TRUE(WaitFor([&] { return manager.State() == ConnState::AppleServiceMissing; }, 3000ms));
  std::this_thread::sleep_for(500ms);
  EXPECT_EQ(manager.State(), ConnState::AppleServiceMissing);
  manager.Stop();
}

TEST(ConnectionManager, SendsMicSettingsAfterCameraAndOnChange) {
  constexpr size_t kMicSettingsIndex = 12;
  RecordingPhone phone;
  ConnectionManager manager({});
  manager.SetMicEnabled(true);
  manager.SetDirect("127.0.0.1", phone.Port());
  manager.Start();
  ASSERT_TRUE(WaitFor([&] { return phone.Count(kMicSettingsIndex) >= 1; }, 5000ms));
  // Pings may arrive at any point; the setup itself is Configure, CameraSettings, MicSettings.
  const auto setup = [&] {
    std::vector<proto::Message> out;
    for (auto& m : phone.Messages()) {
      if (!std::holds_alternative<proto::Ping>(m)) out.push_back(std::move(m));
    }
    return out;
  };
  auto messages = setup();
  EXPECT_EQ(messages[messages.size() - 2].index(), kCameraSettingsIndex);
  EXPECT_TRUE(std::get<proto::MicSettings>(messages.back()).enabled);
  manager.SetMicEnabled(false);
  ASSERT_TRUE(WaitFor([&] { return phone.Count(kMicSettingsIndex) >= 2; }, 2000ms));
  EXPECT_FALSE(std::get<proto::MicSettings>(setup().back()).enabled);
  manager.Stop();
}
