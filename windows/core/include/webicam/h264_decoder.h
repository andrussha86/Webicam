#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <span>

#include "webicam/media_foundation.h"

struct IMFTransform;
struct IMFDXGIDeviceManager;

namespace webicam {

struct DecodedFrame {
  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;  // NV12, may be a texture array
  UINT subresource = 0;
  uint32_t width = 0;   // display size (e.g. 1080, not the coded 1088)
  uint32_t height = 0;
  int64_t timestamp100ns = 0;
};

// Media Foundation H.264 decoder on D3D11 in low-latency mode.
class H264Decoder {
 public:
  enum class Result { Frame, NeedMore, Error };

  static std::unique_ptr<H264Decoder> Create(const D3DContext& d3d);
  ~H264Decoder();

  // Error: input is not Annex B or the MFT failed; call Reset() and feed a keyframe.
  Result Decode(std::span<const uint8_t> annexB, int64_t timestamp100ns, DecodedFrame& out);
  void Reset();

 private:
  H264Decoder() = default;
  bool ConfigureOutput();

  Microsoft::WRL::ComPtr<IMFTransform> transform_;
  Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> manager_;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
};

}  // namespace webicam
