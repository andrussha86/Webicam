#include "thumbnails.h"

namespace webicam::app {

ID3D11ShaderResourceView* ThumbnailCache::Get(std::string_view ref, const BackgroundLibrary& library) {
  if (auto it = views_.find(ref); it != views_.end()) return it->second.Get();
  auto& view = views_[std::string(ref)];  // a failed load stays null and is not retried every frame
  const auto image = library.Resolve(ref, 160, 90);
  if (!image) return nullptr;
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = image->width;
  desc.Height = image->height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_IMMUTABLE;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  const D3D11_SUBRESOURCE_DATA data{image->pixels.data(), image->width * 4, 0};
  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
  if (FAILED(device_->CreateTexture2D(&desc, &data, &texture))) return nullptr;
  device_->CreateShaderResourceView(texture.Get(), nullptr, &view);
  return view.Get();
}

void ThumbnailCache::Forget(std::string_view ref) {
  if (auto it = views_.find(ref); it != views_.end()) views_.erase(it);
}

}  // namespace webicam::app
