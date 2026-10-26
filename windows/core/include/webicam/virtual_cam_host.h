#pragma once

#include <string>

struct IMFVirtualCamera;

namespace webicam {

// Owns the "Webicam" virtual camera (Session lifetime: it disappears when this
// process exits). Requires MFStartup to have been called.
class VirtualCamHost {
 public:
  VirtualCamHost() = default;
  VirtualCamHost(const VirtualCamHost&) = delete;
  VirtualCamHost& operator=(const VirtualCamHost&) = delete;
  ~VirtualCamHost();

  bool Start();
  void Stop();
  // HRESULT text of the last failure, for the UI/log.
  const std::wstring& LastError() const { return lastError_; }

 private:
  IMFVirtualCamera* camera_ = nullptr;
  std::wstring lastError_;
};

}  // namespace webicam
