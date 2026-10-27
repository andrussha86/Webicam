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
  std::string backgroundImage = "builtin:0";  // "builtin:N" or a file in the backgrounds folder
  std::string preset;                         // name of the last applied preset
  bool operator==(const StoredSettings&) const = default;
};

// key=value lines; unknown keys and bad values are ignored, so an old or damaged file
// falls back to defaults field by field.
std::string SerializeSettings(const StoredSettings& settings);
StoredSettings ParseSettings(std::string_view text);

// Shared with presets.ini. Each returns true when the key is its own and the value is valid.
void AppendConfigureKeys(std::string& out, const proto::Configure& configure);
bool ParseConfigureKey(std::string_view key, std::string_view value, proto::Configure& configure);
void AppendCameraKeys(std::string& out, const proto::CameraSettings& camera);
bool ParseCameraKey(std::string_view key, std::string_view value, proto::CameraSettings& camera);
// Files from 0.2 have no background_mode: blur > 0 meant the blur was on.
void ApplyLegacyBackground(proto::CameraSettings& camera, bool sawBackgroundMode);

// %LOCALAPPDATA%\Webicam\settings.ini
std::filesystem::path DefaultSettingsPath();
StoredSettings LoadSettings(const std::filesystem::path& path);
bool SaveSettings(const std::filesystem::path& path, const StoredSettings& settings);

}  // namespace webicam
