#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "webicam/backgrounds.h"

using namespace webicam;

TEST(Backgrounds, SizeAndAlpha) {
  const auto image = RenderBuiltinBackground(0, 64, 36);
  ASSERT_EQ(image.width, 64u);
  ASSERT_EQ(image.height, 36u);
  ASSERT_EQ(image.pixels.size(), 64u * 36 * 4);
  for (size_t i = 3; i < image.pixels.size(); i += 4) ASSERT_EQ(image.pixels[i], 255);
}

TEST(Backgrounds, Deterministic) {
  EXPECT_EQ(RenderBuiltinBackground(3, 64, 36).pixels, RenderBuiltinBackground(3, 64, 36).pixels);
}

TEST(Backgrounds, DistinctIndices) {
  EXPECT_NE(RenderBuiltinBackground(0, 64, 36).pixels, RenderBuiltinBackground(4, 64, 36).pixels);
}

TEST(Backgrounds, CornersFollowPalette) {
  // «Туман»: bottom colour B4B9BE, the light spot is at the top, so the bottom row is the gradient.
  const auto image = RenderBuiltinBackground(4, 640, 360);
  const size_t row = size_t(image.height - 1) * image.width * 4;
  long sum[3] = {0, 0, 0};
  for (uint32_t x = 0; x < image.width; ++x) {
    for (int c = 0; c < 3; ++c) sum[c] += image.pixels[row + x * 4 + c];
  }
  const int expected[3] = {0xBE, 0xB9, 0xB4};  // B, G, R
  for (int c = 0; c < 3; ++c) EXPECT_LE(std::abs(int(sum[c] / long(image.width)) - expected[c]), 6) << c;
}

TEST(Backgrounds, BadIndexEmpty) {
  EXPECT_TRUE(RenderBuiltinBackground(-1, 64, 36).pixels.empty());
  EXPECT_TRUE(RenderBuiltinBackground(kBuiltinBackgroundCount, 64, 36).pixels.empty());
  EXPECT_TRUE(RenderBuiltinBackground(0, 0, 36).pixels.empty());
}

TEST(Backgrounds, NamesInRussian) { EXPECT_EQ(std::string(BuiltinBackgroundName(2)), "Тёплая студия"); }
