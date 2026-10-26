// COM entry points for webicam_vcam.dll. Frame Server creates the registered
// CLSID, receives an IMFActivate and activates the MediaSource from it.
#include <windows.h>
#include <mfapi.h>
#include <wrl/module.h>

#include <string>

#include "attributes_impl.h"
#include "media_source.h"
#include "webicam/vcam_logic.h"

using Microsoft::WRL::ComPtr;

namespace webicam::vcam {

class __declspec(uuid("7C1D4E2A-5B3F-4E8A-9C21-6D0B8F3A1E57")) Activator
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                          Microsoft::WRL::ChainInterfaces<IMFActivate, IMFAttributes>> {
 public:
  HRESULT RuntimeClassInitialize() { return MFCreateAttributes(&attributes_, 1); }

  WEBICAM_IMFATTRIBUTES_FORWARD

  STDMETHODIMP ActivateObject(REFIID riid, void** object) override {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (!source_) {
      HRESULT hr = Microsoft::WRL::MakeAndInitialize<MediaSource>(&source_);
      if (FAILED(hr)) return hr;
    }
    return source_->QueryInterface(riid, object);
  }
  STDMETHODIMP ShutdownObject() override { return S_OK; }
  STDMETHODIMP DetachObject() override {
    source_.Reset();
    return S_OK;
  }

 private:
  ComPtr<IMFAttributes> attributes_;
  ComPtr<MediaSource> source_;
};

CoCreatableClass(Activator);

}  // namespace webicam::vcam

namespace {

HMODULE g_module = nullptr;

std::wstring ClsidKey() { return std::wstring(L"Software\\Classes\\CLSID\\") + webicam::kVcamClsidString; }

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_module = instance;
    DisableThreadLibraryCalls(instance);
  }
  return TRUE;
}

STDAPI DllCanUnloadNow() {
  return Microsoft::WRL::Module<Microsoft::WRL::InProc>::GetModule().GetObjectCount() == 0 ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* object) {
  return Microsoft::WRL::Module<Microsoft::WRL::InProc>::GetModule().GetClassObject(clsid, riid, object);
}

STDAPI DllRegisterServer() {
  wchar_t path[MAX_PATH];
  if (!GetModuleFileNameW(g_module, path, MAX_PATH)) return HRESULT_FROM_WIN32(GetLastError());
  HKEY key = nullptr;
  const std::wstring inproc = ClsidKey() + L"\\InprocServer32";
  LSTATUS status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, inproc.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
  if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
  RegSetValueExW(key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(path), DWORD((wcslen(path) + 1) * sizeof(wchar_t)));
  constexpr wchar_t kModel[] = L"Both";
  RegSetValueExW(key, L"ThreadingModel", 0, REG_SZ, reinterpret_cast<const BYTE*>(kModel), sizeof(kModel));
  RegCloseKey(key);
  return S_OK;
}

STDAPI DllUnregisterServer() {
  const LSTATUS status = RegDeleteTreeW(HKEY_LOCAL_MACHINE, ClsidKey().c_str());
  return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(status);
}
