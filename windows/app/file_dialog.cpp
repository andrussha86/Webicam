#include "file_dialog.h"

#include <shobjidl.h>
#include <wrl/client.h>

#include <thread>

#include "webicam/background_library.h"

namespace webicam::app {

namespace {

std::optional<std::filesystem::path> ShowDialog(HWND owner) {
  Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
    return std::nullopt;
  }
  const COMDLG_FILTERSPEC filter{L"Изображения", L"*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tif;*.tiff;*.heic;*.webp"};
  dialog->SetFileTypes(1, &filter);
  dialog->SetTitle(L"Картинка для фона");
  Microsoft::WRL::ComPtr<IShellItem> item;
  PWSTR path = nullptr;
  if (FAILED(dialog->Show(owner)) || FAILED(dialog->GetResult(&item)) ||
      FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
    return std::nullopt;
  }
  std::filesystem::path result(path);
  CoTaskMemFree(path);
  return result;
}

}  // namespace

void ImagePicker::Open(HWND owner, std::filesystem::path libraryDir) {
  if (Busy()) return;
  auto shared = std::make_shared<Shared>();
  shared_ = shared;
  // Detached: quitting with the dialog open must not wait for it. The thread owns everything it uses.
  std::thread([shared, owner, dir = std::move(libraryDir)] {
    Outcome outcome;
    // The common dialog needs an STA; the process is MTA (Media Foundation), so it gets this thread.
    if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
      if (const auto path = ShowDialog(owner)) outcome.added = BackgroundLibrary(dir).Add(*path);
      CoUninitialize();
    }
    std::lock_guard lock(shared->mutex);
    shared->outcome = std::move(outcome);
  }).detach();
}

bool ImagePicker::Busy() const {
  if (!shared_) return false;
  std::lock_guard lock(shared_->mutex);
  return !shared_->outcome;
}

std::optional<ImagePicker::Outcome> ImagePicker::Take() {
  if (!shared_) return std::nullopt;
  std::optional<Outcome> outcome;
  {
    std::lock_guard lock(shared_->mutex);
    if (!shared_->outcome) return std::nullopt;
    outcome = std::move(shared_->outcome);
  }
  shared_.reset();
  return outcome;
}

}  // namespace webicam::app
