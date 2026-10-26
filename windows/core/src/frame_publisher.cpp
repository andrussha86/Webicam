#include "webicam/frame_publisher.h"

namespace webicam {

FramePublisher::FramePublisher(const wchar_t* sectionName, const wchar_t* eventName)
    : sectionName_(sectionName), eventName_(eventName) {}

FramePublisher::~FramePublisher() {
  if (ring_) ring_->SetHeartbeat(0, false);
  if (event_) CloseHandle(event_);
}

bool FramePublisher::TryAttach() {
  if (ring_) return true;
  auto section = SharedSection::Open(sectionName_, FrameRing::RequiredBytes());
  if (!section) return false;
  auto ring = FrameRing::Attach(section->data());
  if (!ring) return false;
  section_ = std::move(section);
  ring_ = ring;
  if (!event_) event_ = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName_);
  return true;
}

std::optional<OutputFormat> FramePublisher::Requested() const {
  if (!ring_) return std::nullopt;
  const auto format = ring_->RequestedFormat();
  if (!format) return std::nullopt;
  return OutputFormat{(*format)[0], (*format)[1], (*format)[2]};
}

bool FramePublisher::Publish(std::span<const uint8_t> nv12, uint32_t width, uint32_t height, int64_t timestamp100ns) {
  if (!ring_) return false;
  if (!ring_->Publish({width, height, width, timestamp100ns}, nv12)) return false;
  if (event_) SetEvent(event_);
  return true;
}

void FramePublisher::Heartbeat(bool phoneConnected) {
  if (ring_) ring_->SetHeartbeat(SteadyNowMs(), phoneConnected);
}

}  // namespace webicam
