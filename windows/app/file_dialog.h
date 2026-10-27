#pragma once

#include <windows.h>

#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace webicam::app {

// System «Open» dialog filtered to pictures, then BackgroundLibrary::Add, both on a thread of their
// own. The window must keep pumping messages while its owned dialog is open: waiting for the
// dialog on the window's thread deadlocks both.
class ImagePicker {
 public:
  struct Outcome {
    // nullopt = the dialog was cancelled; otherwise the library name or a Russian error.
    std::optional<std::expected<std::string, std::string>> added;
  };

  // No-op while the dialog is already open.
  void Open(HWND owner, std::filesystem::path libraryDir);
  bool Busy() const;
  // The result once, after the dialog closes and the picture is stored.
  std::optional<Outcome> Take();

 private:
  struct Shared {
    std::mutex mutex;
    std::optional<Outcome> outcome;
  };
  std::shared_ptr<Shared> shared_;
};

}  // namespace webicam::app
