#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <thread>

#include "webicam/device_session.h"
#include "webicam/testvectors.h"

using namespace webicam;
using namespace std::chrono_literals;

namespace {

// Loopback pair: `phone` is the test's end, `session` talks to it.
struct Harness {
  Harness() {
    auto listener = Socket::Listen("127.0.0.1", 0);
    const uint16_t port = listener->LocalPort();
    std::thread connector([&] { pcSide = std::move(*Socket::ConnectTcp("127.0.0.1", port, 1000)); });
    phone = listener->Accept(1000);
    connector.join();
  }

  void SendFromPhone(const proto::Message& m) { phone.SendAll(proto::Encode(m)); }

  // Reads messages the PC sent until `pred` matches one or the timeout expires.
  template <typename T>
  std::optional<T> ReadOnPhone(std::chrono::milliseconds timeout = 1000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::array<uint8_t, 4096> buffer{};
    while (std::chrono::steady_clock::now() < deadline) {
      proto::Message m;
      if (parser.Next(m) == proto::MessageParser::Result::Message) {
        if (auto* value = std::get_if<T>(&m)) return *value;
        continue;
      }
      const int r = phone.Recv(buffer, 50);
      if (r > 0) parser.Feed(std::span(buffer.data(), size_t(r)));
      if (r == 0 || r == -1) break;
    }
    return std::nullopt;
  }

  Socket phone;
  Socket pcSide;
  proto::MessageParser parser;
};

template <typename Pred>
bool WaitFor(Pred pred, std::chrono::milliseconds timeout = 2000ms) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) return true;
    std::this_thread::sleep_for(5ms);
  }
  return pred();
}

proto::Message PhoneHello() { return proto::TestVectors()[1].message; }

}  // namespace

TEST(DeviceSession, HandshakeDeliversHello) {
  Harness h;
  std::atomic<size_t> formats{0};
  DeviceSession session(std::move(h.pcSide), {.onHello = [&](const proto::Hello& hello) { formats = hello.formats.size(); }});
  session.Start();
  auto hello = h.ReadOnPhone<proto::Hello>();
  ASSERT_TRUE(hello.has_value());
  EXPECT_EQ(hello->version, proto::kProtocolVersion);
  EXPECT_TRUE(hello->formats.empty());
  h.SendFromPhone(PhoneHello());
  EXPECT_TRUE(WaitFor([&] { return formats == 3; }));
}

TEST(DeviceSession, VersionMismatchCloses) {
  Harness h;
  std::mutex mutex;
  std::string reason;
  DeviceSession session(std::move(h.pcSide), {.onClosed = [&](std::string r) {
                          std::lock_guard lock(mutex);
                          reason = std::move(r);
                        }});
  session.Start();
  h.SendFromPhone(proto::Hello{proto::kProtocolVersion + 1, "iPhone", {}});
  ASSERT_TRUE(WaitFor([&] { return session.Closed(); }));
  std::lock_guard lock(mutex);
  EXPECT_NE(reason.find("версия"), std::string::npos) << reason;
}

TEST(DeviceSession, FramesDelivered) {
  Harness h;
  std::mutex mutex;
  std::vector<uint64_t> times;
  DeviceSession session(std::move(h.pcSide), {.onFrame = [&](proto::VideoFrame&& f) {
                          std::lock_guard lock(mutex);
                          times.push_back(f.captureTimeUs);
                        }});
  session.Start();
  h.SendFromPhone(PhoneHello());
  for (uint64_t t : {10u, 20u, 30u}) h.SendFromPhone(proto::VideoFrame{t, t == 10, {0, 0, 0, 1, 0x65}});
  ASSERT_TRUE(WaitFor([&] {
    std::lock_guard lock(mutex);
    return times.size() == 3;
  }));
  std::lock_guard lock(mutex);
  EXPECT_EQ(times, (std::vector<uint64_t>{10, 20, 30}));
}

TEST(DeviceSession, PingPongGivesOffset) {
  Harness h;
  DeviceSession session(std::move(h.pcSide), {});
  session.Start();
  h.SendFromPhone(PhoneHello());
  auto ping = h.ReadOnPhone<proto::Ping>(2000ms);
  ASSERT_TRUE(ping.has_value());
  h.SendFromPhone(proto::Pong{ping->pcTimeUs, ping->pcTimeUs - 5000});
  ASSERT_TRUE(WaitFor([&] { return session.PhoneToPcUs(0).has_value(); }));
  EXPECT_NEAR(double(*session.PhoneToPcUs(0)), 5000.0, 2000.0);
}

TEST(DeviceSession, PartialMessageThenCloseReportsClosed) {
  Harness h;
  std::atomic<int> frames{0};
  std::atomic<bool> closed{false};
  DeviceSession session(std::move(h.pcSide), {.onFrame = [&](proto::VideoFrame&&) { ++frames; },
                                              .onClosed = [&](std::string) { closed = true; }});
  session.Start();
  h.SendFromPhone(PhoneHello());
  auto bytes = proto::Encode(proto::VideoFrame{1, true, std::vector<uint8_t>(1000, 7)});
  h.phone.SendAll(std::span(bytes).first(bytes.size() / 2));
  std::this_thread::sleep_for(100ms);
  h.phone = Socket();
  EXPECT_TRUE(WaitFor([&] { return closed.load(); }));
  EXPECT_EQ(frames.load(), 0);
}

TEST(DeviceSession, SilentPhoneTimesOut) {
  Harness h;
  std::atomic<bool> closed{false};
  DeviceSession session(std::move(h.pcSide), {.onClosed = [&](std::string) { closed = true; }}, 400);
  session.Start();
  h.SendFromPhone(PhoneHello());
  // The phone never answers pings and sends no frames (app suspended, socket still open).
  EXPECT_TRUE(WaitFor([&] { return closed.load(); }, 2000ms));
}
