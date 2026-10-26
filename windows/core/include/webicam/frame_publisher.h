#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <span>

#include "webicam/frame_ring.h"
#include "webicam/shared_section.h"
#include "webicam/shm_layout.h"
#include "webicam/vcam_logic.h"

namespace webicam {

// Writer side of the shared memory the virtual camera creates.
class FramePublisher {
 public:
  explicit FramePublisher(const wchar_t* sectionName = shm::kSectionName,
                          const wchar_t* eventName = shm::kFrameEventName);
  FramePublisher(const FramePublisher&) = delete;
  FramePublisher& operator=(const FramePublisher&) = delete;
  ~FramePublisher();

  // Opens the section if a consumer has started the camera; false (no throw) otherwise.
  bool TryAttach();
  bool Attached() const { return ring_.has_value(); }
  // What the consumer asked for, if a consumer is active.
  std::optional<OutputFormat> Requested() const;
  bool Publish(std::span<const uint8_t> nv12, uint32_t width, uint32_t height, int64_t timestamp100ns);
  void Heartbeat(bool phoneConnected);

 private:
  const wchar_t* sectionName_;
  const wchar_t* eventName_;
  std::optional<SharedSection> section_;
  std::optional<FrameRing> ring_;
  HANDLE event_ = nullptr;
};

}  // namespace webicam
