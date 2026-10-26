#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "webicam/h264_decoder.h"

namespace webicam {

// GPU scaling with the D3D11 video processor.
class FrameScaler {
 public:
  static std::unique_ptr<FrameScaler> Create(const D3DContext& d3d);

  // Aspect-fill (center crop) into outW×outH NV12, read back to CPU with stride = outW.
  bool ToNv12Cpu(const DecodedFrame& in, uint32_t outW, uint32_t outH, std::vector<uint8_t>& out);
  // Aspect-fit into at most maxW×maxH BGRA for the UI; the view stays valid until the next call.
  ID3D11ShaderResourceView* ToPreview(const DecodedFrame& in, uint32_t maxW, uint32_t maxH);

 private:
  struct Target {
    uint32_t width = 0, height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> view;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
  };

  FrameScaler() = default;
  bool EnsureProcessor(uint32_t inW, uint32_t inH, uint32_t outW, uint32_t outH);
  bool EnsureTarget(Target& target, uint32_t width, uint32_t height, DXGI_FORMAT format);
  bool Blit(const DecodedFrame& in, Target& target, const RECT& source, const RECT& dest);

  D3DContext d3d_;
  Microsoft::WRL::ComPtr<ID3D11VideoDevice> videoDevice_;
  Microsoft::WRL::ComPtr<ID3D11VideoContext> videoContext_;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> enumerator_;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessor> processor_;
  uint32_t procInW_ = 0, procInH_ = 0, procOutW_ = 0, procOutH_ = 0;
  Target nv12_;
  Target preview_;
};

}  // namespace webicam
