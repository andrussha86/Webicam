#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "webicam/frame_ring.h"

namespace webicam {

inline constexpr wchar_t kVcamClsidString[] = L"{7C1D4E2A-5B3F-4E8A-9C21-6D0B8F3A1E57}";
inline constexpr wchar_t kVcamFriendlyName[] = L"Webicam";
inline constexpr uint64_t kHeartbeatTimeoutMs = 1000;

struct OutputFormat {
  uint32_t width;
  uint32_t height;
  uint32_t fps;
  bool operator==(const OutputFormat&) const = default;
};

// Formats the virtual camera advertises; 1080p30 first (the default), then portrait and 4:3 ones.
std::span<const OutputFormat> AdvertisedFormats();

enum class OutputKind { Frame, NoSignalDimmed, Placeholder };

// Decides what the virtual camera hands to the consumer for the next sample.
OutputKind ChooseOutput(uint64_t nowMs, uint64_t heartbeatMs, bool connected, std::optional<FrameInfo> latest,
                        uint32_t requestedWidth, uint32_t requestedHeight);

uint64_t SteadyNowMs();

}  // namespace webicam
