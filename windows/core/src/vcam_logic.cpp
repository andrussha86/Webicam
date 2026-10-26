#include "webicam/vcam_logic.h"

#include <windows.h>

#include <array>

namespace webicam {

std::span<const OutputFormat> AdvertisedFormats() {
  static constexpr std::array<OutputFormat, 7> kFormats{{
      {1920, 1080, 30},
      {1920, 1080, 60},
      {1280, 720, 30},
      {1280, 720, 60},
      {640, 360, 30},
      {640, 360, 60},
      {3840, 2160, 30},
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
