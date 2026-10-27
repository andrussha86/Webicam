#include <gtest/gtest.h>

#include <vector>

#include "webicam/audio_output.h"

using namespace webicam;

namespace {

std::vector<int16_t> Ramp(size_t n, int16_t start = 1) {
  std::vector<int16_t> v(n);
  for (size_t i = 0; i < n; ++i) v[i] = int16_t(start + int(i));
  return v;
}

}  // namespace

TEST(AudioJitterBuffer, SilentUntilTargetThenPlaysInOrder) {
  AudioJitterBuffer buffer(4, 100);
  std::vector<int16_t> out(3, 99);
  buffer.Push(Ramp(3));
  buffer.Pull(out);
  EXPECT_EQ(out, (std::vector<int16_t>{0, 0, 0}));  // 3 < target: still priming
  buffer.Push(Ramp(2, 4));
  buffer.Pull(out);
  EXPECT_EQ(out, (std::vector<int16_t>{1, 2, 3}));
  EXPECT_EQ(buffer.Buffered(), 2u);
}

TEST(AudioJitterBuffer, RunningDryPadsWithSilenceAndPrimesAgain) {
  AudioJitterBuffer buffer(2, 100);
  buffer.Push(Ramp(3));
  std::vector<int16_t> out(5, 99);
  buffer.Pull(out);
  EXPECT_EQ(out, (std::vector<int16_t>{1, 2, 3, 0, 0}));
  buffer.Push(Ramp(1, 7));
  std::vector<int16_t> one(1, 99);
  buffer.Pull(one);
  EXPECT_EQ(one[0], 0);  // one sample is below the target again
}

TEST(AudioJitterBuffer, TooFarBehindDropsOldestDownToTarget) {
  AudioJitterBuffer buffer(4, 10);
  buffer.Push(Ramp(11));
  EXPECT_EQ(buffer.Buffered(), 4u);
  std::vector<int16_t> out(4);
  buffer.Pull(out);
  EXPECT_EQ(out, (std::vector<int16_t>{8, 9, 10, 11}));  // newest kept
}

TEST(AudioJitterBuffer, LevelFollowsPeakAndResets) {
  AudioJitterBuffer buffer(4, 100);
  buffer.Push(std::vector<int16_t>{0, -16384, 100});
  EXPECT_NEAR(buffer.Level(), 0.5f, 0.001f);
  buffer.Push(std::vector<int16_t>{0});
  EXPECT_LT(buffer.Level(), 0.5f);
  buffer.Reset();
  EXPECT_EQ(buffer.Level(), 0.0f);
  EXPECT_EQ(buffer.Buffered(), 0u);
}

TEST(AudioDevices, FindsVirtualCableByName) {
  const std::vector<AudioDevice> devices{{"a", "Speakers (Realtek)"}, {"b", "CABLE Input (VB-Audio Virtual Cable)"}};
  EXPECT_EQ(FindVirtualCable(devices), "b");
  EXPECT_EQ(FindVirtualCable({{"a", "Speakers"}}), "");
}
