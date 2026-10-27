#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "webicam/protocol.h"

namespace webicam {

// A named snapshot of everything the user tunes: format, camera and background.
struct Preset {
  std::string name;  // UTF-8
  bool builtin = false;
  proto::Configure configure{1920, 1080, 30, 16000};
  proto::CameraSettings camera;
  std::string backgroundImage = "builtin:0";
  bool operator==(const Preset&) const = default;
};

// «Созвон», «Стрим», «Тёмная комната»; never saved, never deleted.
const std::vector<Preset>& BuiltinPresets();

// presets.ini: one [name] section per user preset with settings.ini keys inside.
std::string SerializePresets(const std::vector<Preset>& user);
// Sections with a bad or missing format are skipped; the rest load.
std::vector<Preset> ParsePresets(std::string_view text);

enum class PresetNameError { None, Empty, TooLong, BadChar, Builtin, Exists };
// Up to 32 characters, no [ ] or line breaks, unique ignoring case among built-ins and `user`.
PresetNameError ValidatePresetName(std::string_view name, const std::vector<Preset>& user, bool allowOverwrite);
bool SameNameIgnoreCase(std::string_view a, std::string_view b);

// The exact format if the phone has it; else the same size at min(wanted fps, phone max fps);
// nullopt when the phone lacks the size.
std::optional<proto::Configure> MatchFormat(const proto::Configure& wanted,
                                            const std::vector<proto::CaptureFormat>& phone);

bool PresetMatches(const Preset& preset, const proto::Configure& configure, const proto::CameraSettings& camera,
                   std::string_view backgroundImage);

// %LOCALAPPDATA%\Webicam\presets.ini
std::filesystem::path DefaultPresetsPath();
std::vector<Preset> LoadPresets(const std::filesystem::path& path);
bool SavePresets(const std::filesystem::path& path, const std::vector<Preset>& user);

}  // namespace webicam
