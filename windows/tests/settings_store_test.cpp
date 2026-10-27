#include <gtest/gtest.h>

#include "webicam/settings_store.h"

using namespace webicam;

TEST(SettingsStore, RoundTrip) {
  StoredSettings s;
  s.configure = proto::Configure{3840, 2160, 30, 45000};
  s.camera = {proto::Lens::Front, 1.5f, true, 640.0f, 4000, -1.5f, true, 3200.0f, -12.5f, true, 0.125f, 70, true};
  EXPECT_EQ(ParseSettings(SerializeSettings(s)), s);
}

TEST(SettingsStore, DefaultsWithoutConfigure) {
  StoredSettings s;
  const auto parsed = ParseSettings(SerializeSettings(s));
  EXPECT_FALSE(parsed.configure);
  EXPECT_EQ(parsed.camera, proto::CameraSettings{});
}

TEST(SettingsStore, BadValuesFallBackPerField) {
  const auto parsed = ParseSettings("lens=9\r\niso=abc\r\nblur=101\r\ntint=7\r\nwidth=1920\r\njunk\r\n");
  EXPECT_EQ(parsed.camera.lens, proto::Lens::Wide);
  EXPECT_EQ(parsed.camera.iso, proto::CameraSettings{}.iso);
  EXPECT_EQ(parsed.camera.blur, 0);
  EXPECT_EQ(parsed.camera.tint, 7.0f);
  EXPECT_FALSE(parsed.configure);  // incomplete
}

TEST(SettingsStore, RoundTripV3) {
  StoredSettings s;
  s.configure = proto::Configure{1280, 720, 30, 4000};
  s.camera.faceFocus = true;
  s.camera.backgroundMode = proto::BackgroundMode::Color;
  s.camera.backgroundColor = 0x336699;
  s.backgroundImage = "builtin:5";
  s.preset = "Мой стол";
  EXPECT_EQ(ParseSettings(SerializeSettings(s)), s);
}

TEST(SettingsStore, LegacyBlurMapsToBlurMode) {
  const auto s = ParseSettings("blur=40\n");
  EXPECT_EQ(s.camera.backgroundMode, proto::BackgroundMode::Blur);
  EXPECT_EQ(s.camera.blur, 40);
}

TEST(SettingsStore, LegacyNoBlurIsOff) {
  EXPECT_EQ(ParseSettings("blur=0\n").camera.backgroundMode, proto::BackgroundMode::Off);
}

TEST(SettingsStore, BadBackgroundModeIgnored) {
  const auto s = ParseSettings("background_mode=9\nbackground_color=zz\n");
  EXPECT_EQ(s.camera.backgroundMode, proto::BackgroundMode::Off);
  EXPECT_EQ(s.camera.backgroundColor, proto::CameraSettings{}.backgroundColor);
}
