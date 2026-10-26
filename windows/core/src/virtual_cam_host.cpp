#include "webicam/virtual_cam_host.h"

#include <windows.h>
#include <mfvirtualcamera.h>

#include <cstdio>

#include "webicam/vcam_logic.h"

namespace webicam {

namespace {

std::wstring Describe(const wchar_t* step, HRESULT hr) {
  wchar_t text[128];
  swprintf_s(text, L"%s failed: 0x%08X", step, unsigned(hr));
  return text;
}

}  // namespace

VirtualCamHost::~VirtualCamHost() { Stop(); }

bool VirtualCamHost::Start() {
  if (camera_) return true;
  HRESULT hr = MFCreateVirtualCamera(MFVirtualCameraType_SoftwareCameraSource, MFVirtualCameraLifetime_Session,
                                     MFVirtualCameraAccess_CurrentUser, kVcamFriendlyName, kVcamClsidString, nullptr,
                                     0, &camera_);
  if (FAILED(hr)) {
    lastError_ = Describe(L"MFCreateVirtualCamera", hr);
    camera_ = nullptr;
    return false;
  }
  hr = camera_->Start(nullptr);
  if (FAILED(hr)) {
    lastError_ = Describe(L"IMFVirtualCamera::Start", hr);
    camera_->Release();
    camera_ = nullptr;
    return false;
  }
  return true;
}

void VirtualCamHost::Stop() {
  if (!camera_) return;
  camera_->Stop();
  camera_->Remove();
  camera_->Shutdown();
  camera_->Release();
  camera_ = nullptr;
}

}  // namespace webicam
