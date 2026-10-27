#include "webicam/backgrounds.h"

#include <algorithm>
#include <cmath>

namespace webicam {

namespace {

struct Palette {
  const char* name;
  uint32_t top, bottom;
  float spotX, spotY, spotRadius;  // centre in frame fractions, radius in frame heights
  uint32_t spot;
};

constexpr Palette kPalettes[kBuiltinBackgroundCount] = {
    {"Графит", 0x2A2C30, 0x16171A, 0.30f, 0.25f, 0.60f, 0x3A3D42},
    {"Сумерки", 0x2B3245, 0x15161C, 0.75f, 0.20f, 0.55f, 0x4A5470},
    {"Тёплая студия", 0x3A3029, 0x17140F, 0.35f, 0.30f, 0.60f, 0x6B5442},
    {"Холодная студия", 0x27313A, 0x111518, 0.65f, 0.30f, 0.60f, 0x4B5F70},
    {"Туман", 0xD9DCDF, 0xB4B9BE, 0.50f, 0.20f, 0.70f, 0xEEF0F2},
    {"Мох", 0x2C3529, 0x141812, 0.30f, 0.30f, 0.55f, 0x4A5A40},
    {"Песок", 0xCDBFA8, 0x9E907A, 0.70f, 0.25f, 0.65f, 0xE3D7C3},
    {"Ночь", 0x101318, 0x07080A, 0.50f, 0.35f, 0.50f, 0x1E2633},
};

float Channel(uint32_t rgb, int shift) { return float((rgb >> shift) & 0xFF); }

float Smoothstep(float edge0, float edge1, float x) {
  const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

// PCG output permutation: cheap, well-mixed, and the same on every machine.
uint32_t Hash(uint32_t v) {
  v = v * 747796405u + 2891336453u;
  v = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
  return (v >> 22u) ^ v;
}

}  // namespace

const char* BuiltinBackgroundName(int index) {
  return index >= 0 && index < kBuiltinBackgroundCount ? kPalettes[index].name : "";
}

Bgra RenderBuiltinBackground(int index, uint32_t width, uint32_t height) {
  if (index < 0 || index >= kBuiltinBackgroundCount || width == 0 || height == 0) return {};
  const Palette& p = kPalettes[index];
  Bgra image{width, height, std::vector<uint8_t>(size_t(width) * height * 4)};
  const float h = float(height);
  uint8_t* out = image.pixels.data();
  for (uint32_t y = 0; y < height; ++y) {
    const float t = height > 1 ? float(y) / float(height - 1) : 0.0f;
    for (uint32_t x = 0; x < width; ++x) {
      const float dx = (float(x) - p.spotX * float(width)) / h;
      const float dy = (float(y) - p.spotY * h) / h;
      const float light = 0.8f * (1.0f - Smoothstep(0.0f, p.spotRadius, std::sqrt(dx * dx + dy * dy)));
      for (int c = 0; c < 3; ++c) {
        const int shift = 8 * c;  // B, G, R from 0xRRGGBB
        const float base = Channel(p.top, shift) + (Channel(p.bottom, shift) - Channel(p.top, shift)) * t;
        const float lit = base + (Channel(p.spot, shift) - base) * light;
        const uint32_t seed = x + y * 65536u + uint32_t(index) * 0x80000000u + uint32_t(c) * 0x9E3779B9u;
        const float grain = float(Hash(seed)) / 4294967296.0f * 3.0f - 1.5f;
        *out++ = uint8_t(std::clamp(std::lround(lit + grain), 0L, 255L));
      }
      *out++ = 255;
    }
  }
  return image;
}

}  // namespace webicam
