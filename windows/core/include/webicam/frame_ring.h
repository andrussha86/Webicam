#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "webicam/shm_layout.h"

namespace webicam {

struct FrameInfo {
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  int64_t timestamp100ns;
};

// Lock-free "latest frame wins" exchange over a shared memory region.
// One writer (webicam.exe), one reader (webicam_vcam.dll).
class FrameRing {
 public:
  static size_t RequiredBytes();
  static FrameRing Initialize(void* mem);
  static std::optional<FrameRing> Attach(void* mem);

  // Writer side.
  bool Publish(const FrameInfo& info, std::span<const uint8_t> nv12);
  void SetHeartbeat(uint64_t nowMs, bool connected);
  std::optional<std::array<uint32_t, 3>> RequestedFormat() const;

  // Reader side. Copies the newest frame if its generation is greater than
  // lastGeneration and returns that generation.
  std::optional<uint64_t> ReadLatest(uint64_t lastGeneration, FrameInfo& info,
                                     std::vector<uint8_t>& out);
  void SetRequestedFormat(uint32_t width, uint32_t height, uint32_t fps);
  uint64_t HeartbeatMs() const;
  bool Connected() const;

 private:
  explicit FrameRing(uint8_t* base) : base_(base) {}
  shm::ControlBlock& control() const;
  shm::SlotHeader& slot(uint32_t index) const;
  uint8_t* slotData(uint32_t index) const;

  uint8_t* base_;
  uint64_t nextGeneration_ = 1;
};

}  // namespace webicam
