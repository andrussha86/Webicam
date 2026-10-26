#include "webicam/frame_scaler.h"

#include <algorithm>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace webicam {

namespace {

// Largest centered rect of the source with the destination's aspect ratio (even coordinates).
RECT CenterCrop(uint32_t inW, uint32_t inH, uint32_t outW, uint32_t outH) {
  uint32_t cropW = inW, cropH = inH;
  if (uint64_t(inW) * outH > uint64_t(inH) * outW) {
    cropW = uint32_t(uint64_t(inH) * outW / outH) & ~1u;
  } else {
    cropH = uint32_t(uint64_t(inW) * outH / outW) & ~1u;
  }
  const LONG x = LONG((inW - cropW) / 2) & ~1;
  const LONG y = LONG((inH - cropH) / 2) & ~1;
  return {x, y, x + LONG(cropW), y + LONG(cropH)};
}

}  // namespace

std::unique_ptr<FrameScaler> FrameScaler::Create(const D3DContext& d3d) {
  std::unique_ptr<FrameScaler> scaler(new FrameScaler());
  scaler->d3d_ = d3d;
  if (FAILED(d3d.device.As(&scaler->videoDevice_)) || FAILED(d3d.context.As(&scaler->videoContext_))) return nullptr;
  return scaler;
}

bool FrameScaler::EnsureProcessor(uint32_t inW, uint32_t inH, uint32_t outW, uint32_t outH) {
  if (processor_ && inW == procInW_ && inH == procInH_ && outW == procOutW_ && outH == procOutH_) return true;
  D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc{};
  desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  desc.InputWidth = inW;
  desc.InputHeight = inH;
  desc.OutputWidth = outW;
  desc.OutputHeight = outH;
  desc.Usage = D3D11_VIDEO_USAGE_OPTIMAL_SPEED;
  enumerator_.Reset();
  processor_.Reset();
  if (FAILED(videoDevice_->CreateVideoProcessorEnumerator(&desc, &enumerator_))) return false;
  if (FAILED(videoDevice_->CreateVideoProcessor(enumerator_.Get(), 0, &processor_))) return false;
  videoContext_->VideoProcessorSetStreamAutoProcessingMode(processor_.Get(), 0, FALSE);
  videoContext_->VideoProcessorSetStreamFrameFormat(processor_.Get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
  procInW_ = inW;
  procInH_ = inH;
  procOutW_ = outW;
  procOutH_ = outH;
  // Views are tied to the enumerator; force their recreation.
  nv12_.view.Reset();
  preview_.view.Reset();
  return true;
}

bool FrameScaler::EnsureTarget(Target& target, uint32_t width, uint32_t height, DXGI_FORMAT format) {
  if (target.texture && target.width == width && target.height == height && target.format == format) {
    if (!target.view) {
      D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC viewDesc{D3D11_VPOV_DIMENSION_TEXTURE2D};
      if (FAILED(videoDevice_->CreateVideoProcessorOutputView(target.texture.Get(), enumerator_.Get(), &viewDesc,
                                                              &target.view))) {
        return false;
      }
    }
    return true;
  }
  target = Target{width, height, format};
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET | (format == DXGI_FORMAT_B8G8R8A8_UNORM ? D3D11_BIND_SHADER_RESOURCE : 0);
  if (FAILED(d3d_.device->CreateTexture2D(&desc, nullptr, &target.texture))) return false;
  if (format == DXGI_FORMAT_NV12) {
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(d3d_.device->CreateTexture2D(&desc, nullptr, &target.staging))) return false;
  } else if (FAILED(d3d_.device->CreateShaderResourceView(target.texture.Get(), nullptr, &target.srv))) {
    return false;
  }
  D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC viewDesc{D3D11_VPOV_DIMENSION_TEXTURE2D};
  return SUCCEEDED(
      videoDevice_->CreateVideoProcessorOutputView(target.texture.Get(), enumerator_.Get(), &viewDesc, &target.view));
}

bool FrameScaler::Blit(const DecodedFrame& in, Target& target, const RECT& source, const RECT& dest) {
  D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputDesc{};
  inputDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
  inputDesc.Texture2D.ArraySlice = in.subresource;
  ComPtr<ID3D11VideoProcessorInputView> inputView;
  if (FAILED(videoDevice_->CreateVideoProcessorInputView(in.texture.Get(), enumerator_.Get(), &inputDesc, &inputView))) {
    return false;
  }
  D3D11_VIDEO_PROCESSOR_COLOR_SPACE inputSpace{};
  inputSpace.YCbCr_Matrix = 1;  // BT.709
  inputSpace.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
  videoContext_->VideoProcessorSetStreamColorSpace(processor_.Get(), 0, &inputSpace);
  D3D11_VIDEO_PROCESSOR_COLOR_SPACE outputSpace = inputSpace;
  if (target.format == DXGI_FORMAT_B8G8R8A8_UNORM) {
    outputSpace = {};
    outputSpace.RGB_Range = 0;  // full range RGB
  }
  videoContext_->VideoProcessorSetOutputColorSpace(processor_.Get(), &outputSpace);
  videoContext_->VideoProcessorSetStreamSourceRect(processor_.Get(), 0, TRUE, &source);
  videoContext_->VideoProcessorSetStreamDestRect(processor_.Get(), 0, TRUE, &dest);
  const RECT full{0, 0, LONG(target.width), LONG(target.height)};
  videoContext_->VideoProcessorSetOutputTargetRect(processor_.Get(), TRUE, &full);
  const D3D11_VIDEO_COLOR black{.YCbCr = {0.0625f, 0.5f, 0.5f, 1.0f}};
  videoContext_->VideoProcessorSetOutputBackgroundColor(processor_.Get(),
                                                        target.format == DXGI_FORMAT_NV12 ? TRUE : FALSE, &black);
  D3D11_VIDEO_PROCESSOR_STREAM stream{};
  stream.Enable = TRUE;
  stream.pInputSurface = inputView.Get();
  return SUCCEEDED(videoContext_->VideoProcessorBlt(processor_.Get(), target.view.Get(), 0, 1, &stream));
}

bool FrameScaler::ToNv12Cpu(const DecodedFrame& in, uint32_t outW, uint32_t outH, std::vector<uint8_t>& out) {
  if (!in.texture || in.width == 0 || in.height == 0 || outW == 0 || outH == 0) return false;
  if (!EnsureProcessor(in.width, in.height, outW, outH)) return false;
  if (!EnsureTarget(nv12_, outW, outH, DXGI_FORMAT_NV12)) return false;
  const RECT dest{0, 0, LONG(outW), LONG(outH)};
  if (!Blit(in, nv12_, CenterCrop(in.width, in.height, outW, outH), dest)) return false;

  d3d_.context->CopyResource(nv12_.staging.Get(), nv12_.texture.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(d3d_.context->Map(nv12_.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
  out.resize(size_t(outW) * outH * 3 / 2);
  const auto* src = static_cast<const uint8_t*>(mapped.pData);
  // NV12 staging layout: Y plane rows, then UV rows at RowPitch * height.
  for (uint32_t row = 0; row < outH; ++row) {
    std::memcpy(out.data() + size_t(row) * outW, src + size_t(row) * mapped.RowPitch, outW);
  }
  const uint8_t* uv = src + size_t(mapped.RowPitch) * outH;
  uint8_t* uvOut = out.data() + size_t(outW) * outH;
  for (uint32_t row = 0; row < outH / 2; ++row) {
    std::memcpy(uvOut + size_t(row) * outW, uv + size_t(row) * mapped.RowPitch, outW);
  }
  d3d_.context->Unmap(nv12_.staging.Get(), 0);
  return true;
}

ID3D11ShaderResourceView* FrameScaler::ToPreview(const DecodedFrame& in, uint32_t maxW, uint32_t maxH) {
  if (!in.texture || in.width == 0 || in.height == 0 || maxW < 2 || maxH < 2) return nullptr;
  // Fit inside maxW×maxH preserving aspect.
  uint32_t w = maxW, h = uint32_t(uint64_t(maxW) * in.height / in.width);
  if (h > maxH) {
    h = maxH;
    w = uint32_t(uint64_t(maxH) * in.width / in.height);
  }
  w = std::max(2u, w & ~1u);
  h = std::max(2u, h & ~1u);
  if (!EnsureProcessor(in.width, in.height, w, h)) return nullptr;
  if (!EnsureTarget(preview_, w, h, DXGI_FORMAT_B8G8R8A8_UNORM)) return nullptr;
  const RECT source{0, 0, LONG(in.width), LONG(in.height)};
  const RECT dest{0, 0, LONG(w), LONG(h)};
  if (!Blit(in, preview_, source, dest)) return nullptr;
  return preview_.srv.Get();
}

}  // namespace webicam
