#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <map>
#include <string>
#include <string_view>

#include "webicam/background_library.h"

namespace webicam::app {

// 160×90 previews of backgrounds as GPU textures, created on first use and kept while the window lives.
class ThumbnailCache {
 public:
  explicit ThumbnailCache(ID3D11Device* device) : device_(device) {}
  // nullptr when the picture cannot be read.
  ID3D11ShaderResourceView* Get(std::string_view ref, const BackgroundLibrary& library);
  void Forget(std::string_view ref);

 private:
  ID3D11Device* device_;
  std::map<std::string, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, std::less<>> views_;
};

}  // namespace webicam::app
