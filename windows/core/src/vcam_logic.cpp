#include "webicam/vcam_logic.h"

#include <windows.h>

#include <array>

namespace webicam {

std::span<const OutputFormat> AdvertisedFormats() {
  // Landscape 16:9 first (what most apps pick), then the shapes the phone can also stream:
  // portrait for vertical video and 4:3, both ways round.
  static constexpr std::array<OutputFormat, 15> kFormats{{
      {1920, 1080, 30},
      {1920, 1080, 60},
      {1280, 720, 30},
      {1280, 720, 60},
      {640, 360, 30},
      {640, 360, 60},
      {3840, 2160, 30},
      {1080, 1920, 30},
      {1080, 1920, 60},
      {720, 1280, 30},
      {1440, 1080, 30},
      {1440, 1080, 60},
      {960, 720, 30},
      {640, 480, 30},
      {1080, 1440, 30},
  }};
  return kFormats;
}

OutputKind ChooseOutput(uint64_t nowMs, uint64_t heartbeatMs, bool connected, std::optional<FrameInfo> latest,
                        uint32_t requestedWidth, uint32_t requestedHeight) {
  if (!latest) return OutputKind::Placeholder;
  if (latest->width != requestedWidth || latest->height != requestedHeight) return OutputKind::Placeholder;
  const bool stale = nowMs > heartbeatMs + kHeartbeatTimeoutMs;
  if (stale || !connected) return OutputKind::NoSignalDimmed;
  return OutputKind::Frame;
}

uint64_t SteadyNowMs() { return GetTickCount64(); }

}  // namespace webicam
