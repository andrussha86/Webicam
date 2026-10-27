#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <vector>

#include "webicam/backgrounds.h"

namespace webicam {

// WIC-based; the calling thread must have COM initialised.
std::optional<Bgra> LoadImageFile(const std::filesystem::path& path);
std::optional<Bgra> DecodeImage(std::span<const uint8_t> bytes);
// Crops the source to the target aspect around its centre, then scales (Fant).
Bgra CoverResize(const Bgra& source, uint32_t width, uint32_t height);
std::vector<uint8_t> EncodeJpeg(const Bgra& image, float quality = 0.85f);

}  // namespace webicam
