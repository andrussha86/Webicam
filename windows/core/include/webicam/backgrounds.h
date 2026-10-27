#pragma once

#include <cstdint>
#include <vector>

namespace webicam {

// 8-bit B, G, R, A; stride = width * 4.
struct Bgra {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> pixels;
};

inline constexpr int kBuiltinBackgroundCount = 8;

// Russian display name, or "" for a bad index.
const char* BuiltinBackgroundName(int index);
// Soft gradient with a light spot and fixed-seed grain (keeps H.264 from banding); empty on a bad
// index or zero size. The same arguments always give the same pixels.
Bgra RenderBuiltinBackground(int index, uint32_t width, uint32_t height);

}  // namespace webicam
