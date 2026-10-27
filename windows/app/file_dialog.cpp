#include "file_dialog.h"

#include <shobjidl.h>
#include <wrl/client.h>

#include <thread>

namespace webicam::app {

std::optional<std::filesystem::path> PickImageFile(HWND owner) {
  // The common dialog needs an STA; the process is MTA (Media Foundation), so it gets its own thread.
  std::optional<std::filesystem::path> result;
  std::thread([&] {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) return;
    {
      Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
      if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        const COMDLG_FILTERSPEC filter{L"Изображения", L"*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tif;*.tiff;*.heic"};
        dialog->SetFileTypes(1, &filter);
        dialog->SetTitle(L"Картинка для фона");
        Microsoft::WRL::ComPtr<IShellItem> item;
        PWSTR path = nullptr;
        if (SUCCEEDED(dialog->Show(owner)) && SUCCEEDED(dialog->GetResult(&item)) &&
            SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
          result = std::filesystem::path(path);
          CoTaskMemFree(path);
        }
      }
    }
    CoUninitialize();
  }).join();
  return result;
}

}  // namespace webicam::app
