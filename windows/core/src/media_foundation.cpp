#include "webicam/media_foundation.h"

#include <mfapi.h>

#include <mutex>

namespace webicam {

void EnsureMediaFoundation() {
  static std::once_flag once;
  std::call_once(once, [] {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);
  });
}

std::optional<D3DContext> D3DContext::Create() {
  D3DContext ctx;
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2, D3D11_SDK_VERSION,
                                 &ctx.device, nullptr, &ctx.context);
  if (FAILED(hr)) {
    // No hardware video support (e.g. CI runners): fall back to WARP.
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2,
                           D3D11_SDK_VERSION, &ctx.device, nullptr, &ctx.context);
  }
  if (FAILED(hr)) return std::nullopt;
  Microsoft::WRL::ComPtr<ID3D10Multithread> multithread;
  if (SUCCEEDED(ctx.device.As(&multithread))) multithread->SetMultithreadProtected(TRUE);
  return ctx;
}

}  // namespace webicam
