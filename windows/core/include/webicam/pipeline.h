#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

#include "webicam/frame_publisher.h"
#include "webicam/frame_scaler.h"
#include "webicam/h264_decoder.h"
#include "webicam/protocol.h"

namespace webicam {

struct PipelineStats {
  double fps = 0;
  double latencyMs = 0;  // capture on the phone → published to shared memory
  uint64_t droppedFrames = 0;
  uint32_t width = 0;    // decoded size
  uint32_t height = 0;
  std::optional<OutputFormat> output;  // what the consumer receives
};

// Decode → scale to the consumer's format → publish. OnFrame runs on the session
// thread; Tick and Preview on the UI thread.
class Pipeline {
 public:
  Pipeline(D3DContext d3d, std::function<void()> requestKeyframe,
           std::function<std::optional<int64_t>(uint64_t)> phoneToPcUs);

  bool Ready() const { return decoder_ && scaler_; }
  void OnFrame(proto::VideoFrame&& frame);
  void OnConnectionChanged(bool connected);
  // Heartbeat and shared-memory attach retry; call at least every 500 ms.
  void Tick();
  ID3D11ShaderResourceView* Preview(uint32_t maxW, uint32_t maxH);
  PipelineStats Stats() const;

 private:
  D3DContext d3d_;
  std::function<void()> requestKeyframe_;
  std::function<std::optional<int64_t>(uint64_t)> phoneToPcUs_;
  std::unique_ptr<H264Decoder> decoder_;
  std::unique_ptr<FrameScaler> scaler_;
  FramePublisher publisher_;

  mutable std::mutex mutex_;  // guards everything below and GPU use
  bool waitingForKeyframe_ = true;
  bool connected_ = false;
  uint64_t lastAttachTryMs_ = 0;
  std::optional<DecodedFrame> last_;
  std::vector<uint8_t> scratch_;
  PipelineStats stats_;
  uint64_t windowStartUs_ = 0;
  uint32_t windowFrames_ = 0;
  double windowLatencySum_ = 0;
  uint32_t windowLatencyCount_ = 0;
};

}  // namespace webicam
