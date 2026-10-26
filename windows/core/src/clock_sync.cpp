#include "webicam/clock_sync.h"

namespace webicam {

void ClockSync::AddSample(uint64_t pcSendUs, uint64_t phoneUs, uint64_t pcRecvUs) {
  if (pcRecvUs < pcSendUs) return;
  const int64_t midpoint = int64_t(pcSendUs + (pcRecvUs - pcSendUs) / 2);
  samples_.push_back({pcRecvUs - pcSendUs, midpoint - int64_t(phoneUs)});
  if (samples_.size() > kWindow) samples_.pop_front();
}

std::optional<int64_t> ClockSync::OffsetUs() const {
  if (samples_.empty()) return std::nullopt;
  const Sample* best = &samples_.front();
  for (const auto& sample : samples_) {
    if (sample.rtt < best->rtt) best = &sample;
  }
  return best->offset;
}

}  // namespace webicam
