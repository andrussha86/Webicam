#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "webicam/backgrounds.h"
#include "webicam/presets.h"

namespace webicam {

// The user's own background pictures, copied into one folder so presets keep working after the
// original file moves. Callers must have COM initialised (WIC).
class BackgroundLibrary {
 public:
  explicit BackgroundLibrary(std::filesystem::path dir);
  const std::filesystem::path& Dir() const { return dir_; }

  // File names, oldest first.
  std::vector<std::string> List() const;
  // Stores a readable picture as <FNV-1a 64 of the file, 16 hex>.jpg, at most 3840 px on its long side;
  // returns its name or a Russian error. Decodes the whole picture: keep it off the UI thread.
  std::expected<std::string, std::string> Add(const std::filesystem::path& source);
  // Refuses while a preset uses the file: «Используется в пресете „…“».
  std::expected<void, std::string> Remove(const std::string& name, const std::vector<Preset>& presets);
  // "builtin:N" or a file name, scaled to cover width×height; nullopt if missing or unreadable.
  std::optional<Bgra> Resolve(std::string_view ref, uint32_t width, uint32_t height) const;

 private:
  std::filesystem::path dir_;
};

// %LOCALAPPDATA%\Webicam\backgrounds
std::filesystem::path DefaultBackgroundsDir();

}  // namespace webicam
