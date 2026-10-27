#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "webicam/protocol.h"

namespace webicam {

// Shape of the stream. The phone reports landscape sensor formats; a portrait aspect is the same
// format turned by 90° on the phone, so 1920×1080 also gives 1080×1920.
enum class Aspect { Wide = 0, Tall = 1, Classic = 2, ClassicTall = 3 };  // 16:9, 9:16, 4:3, 3:4
inline constexpr int kAspectCount = 4;

// The aspect nearest to width×height.
Aspect AspectOf(uint16_t width, uint16_t height);
const char* AspectLabel(Aspect aspect);

// Stream sizes available in `aspect`, smallest first; maxFps is the phone's for that format.
std::vector<proto::CaptureFormat> OutputSizes(const std::vector<proto::CaptureFormat>& phone, Aspect aspect);
// Bit i set = Aspect(i) has at least one size.
uint32_t AvailableAspects(const std::vector<proto::CaptureFormat>& phone);

// Switching to `aspect`: the size whose short side is closest to the current one, at the current
// fps capped by that format, with the default bitrate for it; nullopt when the aspect has no sizes.
std::optional<proto::Configure> ConfigureForAspect(const proto::Configure& current, Aspect aspect,
                                                   const std::vector<proto::CaptureFormat>& phone);

}  // namespace webicam
