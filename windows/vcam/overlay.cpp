#include "overlay.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <tuple>

namespace webicam::vcam {

const std::vector<uint8_t>& TextOverlay::Mask(uint32_t width, uint32_t height, const std::wstring& text) {
  auto key = std::make_tuple(width, height, text);
  if (auto it = cache_.find(key); it != cache_.end()) return it->second;

  std::vector<uint8_t> mask(size_t(width) * height, 0);
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = LONG(width);
  info.bmiHeader.biHeight = -LONG(height);
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HDC dc = CreateCompatibleDC(nullptr);
  HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (dc && bitmap && bits) {
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    std::memset(bits, 0, size_t(width) * height * 4);
    HFONT font = CreateFontW(-int(std::max<uint32_t>(height / 18, 12)), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT rect{0, 0, LONG(width), LONG(height)};
    DrawTextW(dc, text.c_str(), int(text.size()), &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    GdiFlush();
    const auto* pixels = static_cast<const uint8_t*>(bits);
    for (size_t i = 0; i < mask.size(); ++i) mask[i] = pixels[i * 4 + 1];
    SelectObject(dc, oldFont);
    DeleteObject(font);
    SelectObject(dc, oldBitmap);
  }
  if (bitmap) DeleteObject(bitmap);
  if (dc) DeleteDC(dc);
  return cache_.emplace(std::move(key), std::move(mask)).first->second;
}

void TextOverlay::Draw(uint8_t* nv12, uint32_t width, uint32_t height, const std::wstring& text) {
  const auto& mask = Mask(width, height, text);
  for (size_t i = 0; i < mask.size(); ++i) {
    if (mask[i]) nv12[i] = uint8_t(std::max<int>(nv12[i], 16 + mask[i] * 219 / 255));
  }
}

void FillPlaceholder(uint8_t* nv12, uint32_t width, uint32_t height) {
  const size_t luma = size_t(width) * height;
  std::memset(nv12, 32, luma);
  std::memset(nv12 + luma, 128, luma / 2);
}

void Dim(uint8_t* nv12, uint32_t width, uint32_t height) {
  const size_t luma = size_t(width) * height;
  for (size_t i = 0; i < luma; ++i) nv12[i] = uint8_t(16 + (nv12[i] - 16) * 2 / 5);
}

}  // namespace webicam::vcam
