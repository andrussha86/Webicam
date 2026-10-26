#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <optional>

namespace webicam {

// CoInitializeEx(MTA) + MFStartup, once per process.
void EnsureMediaFoundation();

struct D3DContext {
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;

  // Hardware device with BGRA + video support, multithread protected.
  static std::optional<D3DContext> Create();
};

}  // namespace webicam
