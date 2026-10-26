#pragma once

#include <cstdint>
#include <deque>
#include <optional>

namespace webicam {

// Estimates the phone→PC clock offset from ping round trips.
class ClockSync {
 public:
  static constexpr size_t kWindow = 10;

  void AddSample(uint64_t pcSendUs, uint64_t phoneUs, uint64_t pcRecvUs);
  // pcTime ≈ phoneTime + offset, taken from the sample with the smallest RTT.
  std::optional<int64_t> OffsetUs() const;

 private:
  struct Sample {
    uint64_t rtt;
    int64_t offset;
  };
  std::deque<Sample> samples_;
};

}  // namespace webicam
