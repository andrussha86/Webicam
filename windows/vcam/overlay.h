#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace webicam::vcam {

// Renders centered white text into an NV12 frame using GDI. Masks are cached
// per (width, height, text) because rendering is comparatively slow.
class TextOverlay {
 public:
  void Draw(uint8_t* nv12, uint32_t width, uint32_t height, const std::wstring& text);

 private:
  const std::vector<uint8_t>& Mask(uint32_t width, uint32_t height, const std::wstring& text);
  std::map<std::tuple<uint32_t, uint32_t, std::wstring>, std::vector<uint8_t>> cache_;
};

void FillPlaceholder(uint8_t* nv12, uint32_t width, uint32_t height);
void Dim(uint8_t* nv12, uint32_t width, uint32_t height);

}  // namespace webicam::vcam
