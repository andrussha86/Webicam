#include <gtest/gtest.h>

#include <algorithm>

#include "webicam/vcam_logic.h"

using namespace webicam;

namespace {
constexpr FrameInfo k1080{1920, 1080, 1920, 0};
constexpr FrameInfo k720{1280, 720, 1280, 0};
}  // namespace

TEST(ChooseOutput, FreshMatchingFrame) {
  EXPECT_EQ(ChooseOutput(10'000, 9'500, true, k1080, 1920, 1080), OutputKind::Frame);
}

TEST(ChooseOutput, StaleHeartbeatDims) {
  EXPECT_EQ(ChooseOutput(10'000, 8'999, true, k1080, 1920, 1080), OutputKind::NoSignalDimmed);
}

TEST(ChooseOutput, DisconnectedDims) {
  EXPECT_EQ(ChooseOutput(10'000, 9'900, false, k1080, 1920, 1080), OutputKind::NoSignalDimmed);
}

TEST(ChooseOutput, NoFrameEverGivesPlaceholder) {
  EXPECT_EQ(ChooseOutput(10'000, 9'900, true, std::nullopt, 1920, 1080), OutputKind::Placeholder);
}

TEST(ChooseOutput, SizeMismatchGivesPlaceholder) {
  EXPECT_EQ(ChooseOutput(10'000, 9'900, true, k720, 1920, 1080), OutputKind::Placeholder);
}

TEST(AdvertisedFormats, LandscapeFirstAndNo4K60) {
  auto formats = AdvertisedFormats();
  EXPECT_EQ(formats.size(), 15u);
  EXPECT_EQ(formats[0], (OutputFormat{1920, 1080, 30}));
  EXPECT_TRUE(std::ranges::find(formats, OutputFormat{3840, 2160, 30}) != formats.end());
  EXPECT_TRUE(std::ranges::find(formats, OutputFormat{3840, 2160, 60}) == formats.end());
  EXPECT_TRUE(std::ranges::find(formats, OutputFormat{1080, 1920, 30}) != formats.end());
  EXPECT_TRUE(std::ranges::find(formats, OutputFormat{1440, 1080, 30}) != formats.end());
  for (const auto& f : formats) EXPECT_LE(f.height, 2160u) << f.width << "x" << f.height;
}
