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
