#include <gtest/gtest.h>

#include "webicam/presets.h"

using namespace webicam;

namespace {

Preset MakePreset(std::string name) {
  Preset p;
  p.name = std::move(name);
  p.configure = proto::Configure{1920, 1080, 30, 12000};
  p.camera.zoom = 1.5f;
  p.camera.faceFocus = true;
  p.camera.backgroundMode = proto::BackgroundMode::Image;
  p.backgroundImage = "builtin:2";
  return p;
}

}  // namespace

TEST(Presets, RoundTrip) {
  std::vector<Preset> user = {MakePreset("Мой стол"), MakePreset("Вечер")};
  user[1].configure = proto::Configure{1280, 720, 60, 8000};
  user[1].backgroundImage = "0123456789abcdef.png";
  EXPECT_EQ(ParsePresets(SerializePresets(user)), user);
}

TEST(Presets, BrokenSectionSkipped) {
  const auto presets = ParsePresets(
      "[Плохой]\nwidth=abc\nheight=1080\nfps=30\nbitrate_kbps=1000\n"
      "[Хороший]\nwidth=1280\nheight=720\nfps=30\nbitrate_kbps=4000\n");
  ASSERT_EQ(presets.size(), 1u);
  EXPECT_EQ(presets[0].name, "Хороший");
  EXPECT_EQ(presets[0].configure, (proto::Configure{1280, 720, 30, 4000}));
}

TEST(Presets, NameValidation) {
  const std::vector<Preset> none;
  EXPECT_EQ(ValidatePresetName("", none, false), PresetNameError::Empty);
  std::string longName;
  for (int i = 0; i < 33; ++i) longName += "ж";
  EXPECT_EQ(ValidatePresetName(longName, none, false), PresetNameError::TooLong);
  longName.resize(longName.size() - 2);  // 32 Cyrillic letters, 64 bytes
  EXPECT_EQ(ValidatePresetName(longName, none, false), PresetNameError::None);
  EXPECT_EQ(ValidatePresetName("a[b", none, false), PresetNameError::BadChar);
  EXPECT_EQ(ValidatePresetName("a\nb", none, false), PresetNameError::BadChar);
  EXPECT_EQ(ValidatePresetName("Стрим", none, false), PresetNameError::Builtin);
}

TEST(Presets, NameCaseInsensitiveCyrillic) {
  const std::vector<Preset> user = {MakePreset("Мой Стол")};
  EXPECT_EQ(ValidatePresetName("стрим", user, false), PresetNameError::Builtin);
  EXPECT_EQ(ValidatePresetName("мой стол", user, false), PresetNameError::Exists);
  EXPECT_EQ(ValidatePresetName("мой стол", user, true), PresetNameError::None);
}

TEST(Presets, MatchFormatExact) {
  const std::vector<proto::CaptureFormat> phone = {{1280, 720, 60}, {1920, 1080, 60}};
  EXPECT_EQ(MatchFormat({1920, 1080, 30, 9000}, phone), (proto::Configure{1920, 1080, 30, 9000}));
}

TEST(Presets, MatchFormatLowerFps) {
  const std::vector<proto::CaptureFormat> phone = {{1920, 1080, 30}};
  EXPECT_EQ(MatchFormat({1920, 1080, 60, 20000}, phone), (proto::Configure{1920, 1080, 30, 20000}));
}

TEST(Presets, MatchFormatMissingSize) {
  const std::vector<proto::CaptureFormat> phone = {{1920, 1080, 60}};
  EXPECT_FALSE(MatchFormat({3840, 2160, 30, 45000}, phone));
}

TEST(Presets, BuiltinsHaveSpecValues) {
  const auto& b = BuiltinPresets();
  ASSERT_EQ(b.size(), 3u);
  EXPECT_EQ(b[0].name, "Созвон");
  EXPECT_EQ(b[0].configure, (proto::Configure{1280, 720, 30, 4000}));
  EXPECT_EQ(b[0].camera.backgroundMode, proto::BackgroundMode::Blur);
  EXPECT_EQ(b[0].camera.blur, 60);
  EXPECT_EQ(b[1].name, "Стрим");
  EXPECT_EQ(b[1].configure, (proto::Configure{1920, 1080, 60, 20000}));
  EXPECT_EQ(b[2].name, "Тёмная комната");
  EXPECT_EQ(b[2].configure, (proto::Configure{1920, 1080, 30, 10000}));
  EXPECT_FLOAT_EQ(b[2].camera.exposureBias, 1.0f);
  for (const auto& p : b) {
    EXPECT_TRUE(p.builtin);
    EXPECT_TRUE(p.camera.faceFocus);
  }
}

TEST(Presets, MatchesDetectsChange) {
  const auto p = MakePreset("Мой стол");
  EXPECT_TRUE(PresetMatches(p, p.configure, p.camera, p.backgroundImage));
  auto camera = p.camera;
  camera.zoom = 2.0f;
  EXPECT_FALSE(PresetMatches(p, p.configure, camera, p.backgroundImage));
  EXPECT_FALSE(PresetMatches(p, p.configure, p.camera, "builtin:3"));
}
