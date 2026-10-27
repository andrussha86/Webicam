#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "webicam/protocol.h"

namespace webicam {

// What webicam.exe remembers between runs.
struct StoredSettings {
  std::optional<proto::Configure> configure;  // nullopt = pick the default for the phone
  proto::CameraSettings camera;
  bool operator==(const StoredSettings&) const = default;
};

// key=value lines; unknown keys and bad values are ignored, so an old or damaged file
// falls back to defaults field by field.
std::string SerializeSettings(const StoredSettings& settings);
StoredSettings ParseSettings(std::string_view text);

// %LOCALAPPDATA%\Webicam\settings.ini
std::filesystem::path DefaultSettingsPath();
StoredSettings LoadSettings(const std::filesystem::path& path);
bool SaveSettings(const std::filesystem::path& path, const StoredSettings& settings);

}  // namespace webicam
