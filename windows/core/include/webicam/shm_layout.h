#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace webicam::shm {

inline constexpr uint32_t kLayoutVersion = 1;
inline constexpr uint32_t kSlotCount = 3;
inline constexpr uint32_t kMaxWidth = 3840;
inline constexpr uint32_t kMaxHeight = 2160;
inline constexpr size_t kSlotDataBytes = size_t(kMaxWidth) * kMaxHeight * 3 / 2;
inline constexpr wchar_t kSectionName[] = L"Global\\WebicamFrames-v1";
// SYSTEM, LOCAL SERVICE (Frame Server), INTERACTIVE users — full access.
inline constexpr wchar_t kSectionSddl[] = L"D:(A;;GA;;;SY)(A;;GA;;;LS)(A;;GA;;;IU)";

// Auto-reset event the writer signals after each Publish so the camera can
// deliver a new frame without polling.
inline constexpr wchar_t kFrameEventName[] = L"Global\\WebicamFrameReady-v1";

inline constexpr uint32_t kNoSlot = 0xFFFFFFFFu;

struct alignas(64) ControlBlock {
  uint32_t layoutVersion;
  std::atomic<uint64_t> heartbeatMs;
  std::atomic<uint32_t> connected;
  std::atomic<uint32_t> requestedWidth;
  std::atomic<uint32_t> requestedHeight;
  std::atomic<uint32_t> requestedFps;
  std::atomic<uint32_t> readingSlot;
  // (generation << 8) | slot; 0 means nothing published yet.
  std::atomic<uint64_t> latest;
};

struct alignas(64) SlotHeader {
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  int64_t timestamp100ns;
  // 0 while being written.
  std::atomic<uint64_t> generation;
};

inline constexpr size_t kSlotStride = (sizeof(SlotHeader) + kSlotDataBytes + 63) / 64 * 64;
inline constexpr size_t kTotalBytes = sizeof(ControlBlock) + kSlotStride * kSlotCount;

static_assert(std::atomic<uint64_t>::is_always_lock_free);

}  // namespace webicam::shm
