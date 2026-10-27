#include "webicam/frame_formats.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace webicam {

namespace {

constexpr double kRatios[kAspectCount] = {16.0 / 9.0, 9.0 / 16.0, 4.0 / 3.0, 3.0 / 4.0};

bool IsPortrait(Aspect aspect) { return aspect == Aspect::Tall || aspect == Aspect::ClassicTall; }

}  // namespace

Aspect AspectOf(uint16_t width, uint16_t height) {
  if (!width || !height) return Aspect::Wide;
  const double ratio = double(width) / height;
  int best = 0;
  for (int i = 1; i < kAspectCount; ++i) {
    if (std::abs(std::log(ratio / kRatios[i])) < std::abs(std::log(ratio / kRatios[best]))) best = i;
  }
  return Aspect(best);
}

const char* AspectLabel(Aspect aspect) {
  switch (aspect) {
    case Aspect::Wide: return "16:9";
    case Aspect::Tall: return "9:16";
    case Aspect::Classic: return "4:3";
    case Aspect::ClassicTall: return "3:4";
  }
  return "";
}

std::vector<proto::CaptureFormat> OutputSizes(const std::vector<proto::CaptureFormat>& phone, Aspect aspect) {
  std::vector<proto::CaptureFormat> sizes;
  for (const auto& f : phone) {
    // Formats come landscape; anything else is not ours to turn.
    if (f.width < f.height) continue;
    const Aspect landscape = IsPortrait(aspect) ? Aspect(int(aspect) - 1) : aspect;
    if (AspectOf(f.width, f.height) != landscape) continue;
    if (std::abs(double(f.width) / f.height - kRatios[int(landscape)]) > 0.01) continue;
    sizes.push_back(IsPortrait(aspect) ? proto::CaptureFormat{f.height, f.width, f.maxFps} : f);
  }
  std::sort(sizes.begin(), sizes.end(),
            [](const auto& a, const auto& b) { return uint32_t(a.width) * a.height < uint32_t(b.width) * b.height; });
  return sizes;
}

uint32_t AvailableAspects(const std::vector<proto::CaptureFormat>& phone) {
  uint32_t mask = 0;
  for (int i = 0; i < kAspectCount; ++i) {
    if (!OutputSizes(phone, Aspect(i)).empty()) mask |= 1u << i;
  }
  return mask;
}

std::optional<proto::Configure> ConfigureForAspect(const proto::Configure& current, Aspect aspect,
                                                   const std::vector<proto::CaptureFormat>& phone) {
  const auto sizes = OutputSizes(phone, aspect);
  if (sizes.empty()) return std::nullopt;
  const int shortSide = std::min(current.width, current.height);
  const auto best = std::min_element(sizes.begin(), sizes.end(), [&](const auto& a, const auto& b) {
    return std::abs(std::min(a.width, a.height) - shortSide) < std::abs(std::min(b.width, b.height) - shortSide);
  });
  const uint8_t fps = std::min(current.fps ? current.fps : uint8_t(30), best->maxFps);
  return proto::Configure{best->width, best->height, fps, proto::DefaultBitrateKbps(best->width, best->height, fps)};
}

}  // namespace webicam
