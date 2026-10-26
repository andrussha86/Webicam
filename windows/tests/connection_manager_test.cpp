#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

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

}  // namespace

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
