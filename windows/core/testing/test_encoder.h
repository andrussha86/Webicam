#pragma once

#include <cstdint>
#include <memory>
#include <vector>

struct IMFTransform;
struct ICodecAPI;

namespace webicam {

// Software H.264 encoder (Media Foundation) fed with synthetic NV12 frames whose
// luma is uniform: Y = 16 + (n * 7) % 200, UV = 128. Used by tests and fake-phone.
class TestPatternEncoder {
 public:
  static uint8_t LumaFor(uint64_t frameIndex) { return uint8_t(16 + (frameIndex * 7) % 200); }

  static std::unique_ptr<TestPatternEncoder> Create(uint32_t width, uint32_t height, uint32_t fps,
                                                    uint32_t bitrateKbps);
  ~TestPatternEncoder();

  // Encodes the next synthetic frame. Returns Annex B (SPS/PPS included on
  // keyframes) or empty if the encoder buffered it; frameIndex is the index of
  // the frame the returned bytes belong to, keyframe tells whether it is an IDR.
  std::vector<uint8_t> EncodeNext(bool forceKeyframe, uint64_t& frameIndex, bool& keyframe);

  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  uint32_t fps() const { return fps_; }

 private:
  TestPatternEncoder() = default;

  IMFTransform* transform_ = nullptr;
  ICodecAPI* codec_ = nullptr;
  uint32_t width_ = 0, height_ = 0, fps_ = 0;
  uint64_t next_ = 0;
  std::vector<uint8_t> sequenceHeader_;
};

}  // namespace webicam
