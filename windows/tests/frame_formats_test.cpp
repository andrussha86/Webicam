#include <gtest/gtest.h>

#include "webicam/frame_formats.h"

using namespace webicam;

namespace {

const std::vector<proto::CaptureFormat> kPhone = {{1280, 720, 60}, {1920, 1080, 60}, {1920, 1440, 30},
                                                  {3840, 2160, 30}};

}  // namespace

TEST(FrameFormats, AspectOfNearest) {
  EXPECT_EQ(AspectOf(1920, 1080), Aspect::Wide);
  EXPECT_EQ(AspectOf(1080, 1920), Aspect::Tall);
  EXPECT_EQ(AspectOf(1440, 1080), Aspect::Classic);
  EXPECT_EQ(AspectOf(1080, 1440), Aspect::ClassicTall);
}

TEST(FrameFormats, PortraitIsTurnedLandscape) {
  const auto tall = OutputSizes(kPhone, Aspect::Tall);
  ASSERT_EQ(tall.size(), 3u);
  EXPECT_EQ(tall[0], (proto::CaptureFormat{720, 1280, 60}));
  EXPECT_EQ(tall[2], (proto::CaptureFormat{2160, 3840, 30}));
}

TEST(FrameFormats, ClassicOnlyFromNativeFourThree) {
  const auto classic = OutputSizes(kPhone, Aspect::Classic);
  ASSERT_EQ(classic.size(), 1u);
  EXPECT_EQ(classic[0], (proto::CaptureFormat{1920, 1440, 30}));
  EXPECT_EQ(AvailableAspects({{1920, 1080, 60}}), 0b0011u);
  EXPECT_EQ(AvailableAspects(kPhone), 0b1111u);
}

TEST(FrameFormats, SwitchKeepsShortSideAndCapsFps) {
  const auto tall = ConfigureForAspect({1920, 1080, 60, 25000}, Aspect::Tall, kPhone);
  ASSERT_TRUE(tall);
  EXPECT_EQ(*tall, (proto::Configure{1080, 1920, 60, 25000}));
  const auto classic = ConfigureForAspect({1920, 1080, 60, 25000}, Aspect::Classic, kPhone);
  ASSERT_TRUE(classic);
  EXPECT_EQ(classic->width, 1920);
  EXPECT_EQ(classic->height, 1440);
  EXPECT_EQ(classic->fps, 30);
  EXPECT_FALSE(ConfigureForAspect({1920, 1080, 30, 1}, Aspect::Classic, {{1920, 1080, 60}}));
}

TEST(FrameFormats, BitrateByShape) {
  EXPECT_EQ(proto::DefaultBitrateKbps(1080, 1920, 60), 25000u);
  EXPECT_EQ(proto::DefaultBitrateKbps(1920, 1440, 30), 21000u);
}
