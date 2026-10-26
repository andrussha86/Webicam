#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include "webicam/frame_ring.h"

using namespace webicam;

namespace {

struct Region {
  Region() : bytes(new uint8_t[FrameRing::RequiredBytes()]) {}
  std::unique_ptr<uint8_t[]> bytes;
  void* get() { return bytes.get(); }
};

std::vector<uint8_t> Pattern(uint32_t width, uint32_t height) {
  std::vector<uint8_t> data(size_t(width) * height * 3 / 2);
  for (size_t i = 0; i < data.size(); ++i) data[i] = uint8_t(i % 251);
  return data;
}

std::vector<uint8_t> Filled(uint32_t width, uint32_t height, uint8_t value) {
  return std::vector<uint8_t>(size_t(width) * height * 3 / 2, value);
}

}  // namespace

TEST(FrameRing, ReadAfterPublishReturnsFrame) {
  Region region;
  auto writer = FrameRing::Initialize(region.get());
  auto reader = *FrameRing::Attach(region.get());
  auto data = Pattern(64, 32);
  ASSERT_TRUE(writer.Publish({64, 32, 64, 42}, data));

  FrameInfo info{};
  std::vector<uint8_t> out;
  auto generation = reader.ReadLatest(0, info, out);
  ASSERT_TRUE(generation.has_value());
  EXPECT_GT(*generation, 0u);
  EXPECT_EQ(info.width, 64u);
  EXPECT_EQ(info.height, 32u);
  EXPECT_EQ(info.stride, 64u);
  EXPECT_EQ(info.timestamp100ns, 42);
  EXPECT_EQ(out, data);
}

TEST(FrameRing, ReadWithSameGenerationReturnsNullopt) {
  Region region;
  auto writer = FrameRing::Initialize(region.get());
  auto reader = *FrameRing::Attach(region.get());
  ASSERT_TRUE(writer.Publish({64, 32, 64, 1}, Pattern(64, 32)));
  FrameInfo info{};
  std::vector<uint8_t> out;
  auto generation = reader.ReadLatest(0, info, out);
  ASSERT_TRUE(generation.has_value());
  EXPECT_FALSE(reader.ReadLatest(*generation, info, out).has_value());
}

TEST(FrameRing, LatestWins) {
  Region region;
  auto writer = FrameRing::Initialize(region.get());
  auto reader = *FrameRing::Attach(region.get());
  for (uint8_t value : {uint8_t(1), uint8_t(2), uint8_t(3)}) {
    ASSERT_TRUE(writer.Publish({16, 16, 16, value}, Filled(16, 16, value)));
  }
  FrameInfo info{};
  std::vector<uint8_t> out;
  ASSERT_TRUE(reader.ReadLatest(0, info, out).has_value());
  EXPECT_EQ(info.timestamp100ns, 3);
  EXPECT_EQ(out[0], 3);
}

TEST(FrameRing, OversizeRejected) {
  Region region;
  auto writer = FrameRing::Initialize(region.get());
  std::vector<uint8_t> small(16);
  EXPECT_FALSE(writer.Publish({3840, 2161, 3840, 0}, small));
}

TEST(FrameRing, AttachRejectsWrongVersion) {
  Region region;
  FrameRing::Initialize(region.get());
  reinterpret_cast<shm::ControlBlock*>(region.get())->layoutVersion = 99;
  EXPECT_FALSE(FrameRing::Attach(region.get()).has_value());
}

TEST(FrameRing, RequestedFormatRoundTrip) {
  Region region;
  auto writer = FrameRing::Initialize(region.get());
  auto reader = *FrameRing::Attach(region.get());
  EXPECT_FALSE(writer.RequestedFormat().has_value());
  reader.SetRequestedFormat(1920, 1080, 60);
  auto format = writer.RequestedFormat();
  ASSERT_TRUE(format.has_value());
  EXPECT_EQ((*format)[0], 1920u);
  EXPECT_EQ((*format)[1], 1080u);
  EXPECT_EQ((*format)[2], 60u);
}

TEST(FrameRing, ConcurrentNoTornFrames) {
  Region region;
  auto writer = FrameRing::Initialize(region.get());
  auto reader = *FrameRing::Attach(region.get());
  std::atomic<bool> stop{false};
  std::atomic<int> failures{0};
  std::atomic<int> reads{0};

  std::thread readerThread([&] {
    uint64_t lastGeneration = 0;
    int64_t lastTimestamp = -1;
    FrameInfo info{};
    std::vector<uint8_t> out;
    while (!stop.load()) {
      auto generation = reader.ReadLatest(lastGeneration, info, out);
      if (!generation) continue;
      lastGeneration = *generation;
      ++reads;
      const uint8_t expected = uint8_t(info.timestamp100ns % 256);
      for (size_t i = 0; i < out.size(); i += 4093) {
        if (out[i] != expected) {
          ++failures;
          break;
        }
      }
      if (out.back() != expected) ++failures;
      if (info.timestamp100ns < lastTimestamp) ++failures;
      lastTimestamp = info.timestamp100ns;
    }
  });

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  int64_t n = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    writer.Publish({1280, 720, 1280, n}, Filled(1280, 720, uint8_t(n % 256)));
    ++n;
  }
  stop = true;
  readerThread.join();
  EXPECT_EQ(failures.load(), 0);
  EXPECT_GT(reads.load(), 10);
}
