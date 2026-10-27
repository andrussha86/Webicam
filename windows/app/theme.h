#pragma once

#include <imgui.h>

namespace webicam::app::theme {

// Graphite neutrals with one cold steel accent. Colour carries meaning only:
// the accent marks what is on or selected, red marks errors.
inline constexpr ImVec4 Rgb(unsigned hex, float alpha = 1.0f) {
  return ImVec4(float((hex >> 16) & 0xFF) / 255.0f, float((hex >> 8) & 0xFF) / 255.0f, float(hex & 0xFF) / 255.0f,
                alpha);
}

inline constexpr ImVec4 kBase = Rgb(0x0F1012);     // window background, behind the preview
inline constexpr ImVec4 kSurface = Rgb(0x16171A);  // sidebar
inline constexpr ImVec4 kRaised = Rgb(0x1F2125);   // fields, buttons, tracks
inline constexpr ImVec4 kHover = Rgb(0x282A2F);
inline constexpr ImVec4 kActive = Rgb(0x33363C);
inline constexpr ImVec4 kLine = Rgb(0x25272B);
inline constexpr ImVec4 kText = Rgb(0xE6E7E9);
inline constexpr ImVec4 kMuted = Rgb(0x8B9098);
inline constexpr ImVec4 kFaint = Rgb(0x575B62);
inline constexpr ImVec4 kAccent = Rgb(0xA9BACB);
inline constexpr ImVec4 kAccentHi = Rgb(0xC3D0DC);
inline constexpr ImVec4 kOnAccent = Rgb(0x0F1012);
inline constexpr ImVec4 kDanger = Rgb(0xD77272);

// Unscaled font sizes; ImGui multiplies them by the DPI scale.
inline constexpr float kBodySize = 15.0f;
inline constexpr float kSmallSize = 13.0f;
inline constexpr float kTitleSize = 20.0f;

struct Fonts {
  ImFont* regular = nullptr;
  ImFont* medium = nullptr;
  ImFont* semibold = nullptr;
  ImFont* mono = nullptr;
};

// Loads Geist from the exe's resources; falls back to ImGui's own font.
void LoadFonts();
const Fonts& GetFonts();

// Rebuilds the style for a DPI scale (1.0 = 96 dpi). Safe to call again on WM_DPICHANGED.
void Apply(float scale);
float Scale();
// Layout length in pixels at the current DPI.
inline float Px(float v) { return v * Scale(); }

// Windows "Show animations" is off.
bool ReducedMotion();

}  // namespace webicam::app::theme
