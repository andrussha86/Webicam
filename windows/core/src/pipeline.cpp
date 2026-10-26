#include "webicam/pipeline.h"

#include "webicam/device_session.h"

namespace webicam {

Pipeline::Pipeline(D3DContext d3d, std::function<void()> requestKeyframe,
                   std::function<std::optional<int64_t>(uint64_t)> phoneToPcUs)
    : d3d_(std::move(d3d)),
      requestKeyframe_(std::move(requestKeyframe)),
      phoneToPcUs_(std::move(phoneToPcUs)),
      decoder_(H264Decoder::Create(d3d_)),
      scaler_(FrameScaler::Create(d3d_)),
      previewScaler_(FrameScaler::Create(d3d_)) {}

void Pipeline::OnFrame(proto::VideoFrame&& frame) {
  std::lock_guard lock(mutex_);
  if (!Ready()) return;
  if (waitingForKeyframe_ && !frame.keyframe) {
    ++stats_.droppedFrames;
    return;
  }
  waitingForKeyframe_ = false;

  DecodedFrame decoded;
  const auto result = decoder_->Decode(frame.annexB, int64_t(frame.captureTimeUs * 10), decoded);
  if (result == H264Decoder::Result::Error) {
    ++stats_.droppedFrames;
    decoder_->Reset();
    waitingForKeyframe_ = true;
    if (requestKeyframe_) requestKeyframe_();
    return;
  }
  if (result != H264Decoder::Result::Frame) return;
  last_ = decoded;
  stats_.width = decoded.width;
  stats_.height = decoded.height;

  const auto requested = publisher_.Requested();
  stats_.output = requested;
  if (requested && scaler_->ToNv12Cpu(decoded, requested->width, requested->height, scratch_)) {
    publisher_.Publish(scratch_, requested->width, requested->height, decoded.timestamp100ns);
  }

  const uint64_t now = NowUs();
  if (const auto pcCapture = phoneToPcUs_ ? phoneToPcUs_(frame.captureTimeUs) : std::nullopt) {
    windowLatencySum_ += double(int64_t(now) - *pcCapture) / 1000.0;
    ++windowLatencyCount_;
  }
  ++windowFrames_;
  if (windowStartUs_ == 0) windowStartUs_ = now;
  if (now - windowStartUs_ >= 1'000'000) {
    stats_.fps = windowFrames_ * 1e6 / double(now - windowStartUs_);
    stats_.latencyMs = windowLatencyCount_ ? windowLatencySum_ / windowLatencyCount_ : 0;
    windowStartUs_ = now;
    windowFrames_ = 0;
    windowLatencySum_ = 0;
    windowLatencyCount_ = 0;
  }
}

void Pipeline::OnConnectionChanged(bool connected) {
  std::lock_guard lock(mutex_);
  connected_ = connected;
  phoneStreaming_ = true;
  if (connected) {
    waitingForKeyframe_ = true;
    if (decoder_) decoder_->Reset();
  } else {
    stats_.fps = 0;
    stats_.latencyMs = 0;
  }
  publisher_.Heartbeat(connected_);
}

void Pipeline::SetPhoneStreaming(bool streaming) {
  std::lock_guard lock(mutex_);
  phoneStreaming_ = streaming;
  publisher_.Heartbeat(connected_ && phoneStreaming_);
}

void Pipeline::Tick() {
  std::lock_guard lock(mutex_);
  const uint64_t now = SteadyNowMs();
  if (!publisher_.Attached() && now - lastAttachTryMs_ >= 500) {
    lastAttachTryMs_ = now;
    publisher_.TryAttach();
  }
  publisher_.Heartbeat(connected_ && phoneStreaming_);
  if (!publisher_.Requested()) stats_.output.reset();
}

ID3D11ShaderResourceView* Pipeline::Preview(uint32_t maxW, uint32_t maxH) {
  std::lock_guard lock(mutex_);
  if (!last_ || !previewScaler_) return nullptr;
  return previewScaler_->ToPreview(*last_, maxW, maxH);
}

PipelineStats Pipeline::Stats() const {
  std::lock_guard lock(mutex_);
  return stats_;
}

}  // namespace webicam
