#include "webicam/presets.h"

#include <windows.h>

#include <fstream>
#include <iterator>

#include "webicam/settings_store.h"

namespace webicam {

namespace {

Preset Builtin(const char* name, proto::Configure configure) {
  Preset p;
  p.name = name;
  p.builtin = true;
  p.configure = configure;
  p.camera.lens = proto::Lens::Wide;
  p.camera.faceFocus = true;
  return p;
}

std::wstring Wide(std::string_view utf8) {
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
  std::wstring out(size_t(std::max(n, 0)), L'\0');
  if (n > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), out.data(), n);
  return out;
}

size_t CodePoints(std::string_view utf8) {
  size_t n = 0;
  for (unsigned char c : utf8) n += (c & 0xC0) != 0x80;
  return n;
}

bool Complete(const proto::Configure& c) { return c.width && c.height && c.fps && c.bitrateKbps; }

}  // namespace

const std::vector<Preset>& BuiltinPresets() {
  static const std::vector<Preset> presets = [] {
    Preset call = Builtin("Созвон", {1280, 720, 30, 4000});
    call.camera.backgroundMode = proto::BackgroundMode::Blur;
    call.camera.blur = 60;
    Preset stream = Builtin("Стрим", {1920, 1080, 60, 20000});
    Preset dark = Builtin("Тёмная комната", {1920, 1080, 30, 10000});
    dark.camera.exposureBias = 1.0f;
    return std::vector<Preset>{call, stream, dark};
  }();
  return presets;
}

std::string SerializePresets(const std::vector<Preset>& user) {
  std::string out;
  for (const auto& p : user) {
    out += "[" + p.name + "]\n";
    AppendConfigureKeys(out, p.configure);
    out += "background_image=" + p.backgroundImage + "\n";
    AppendCameraKeys(out, p.camera);
    out += "\n";
  }
  return out;
}

std::vector<Preset> ParsePresets(std::string_view text) {
  std::vector<Preset> presets;
  std::optional<Preset> current;
  bool broken = false;
  bool sawBackgroundMode = false;
  const auto flush = [&] {
    if (current && !broken && Complete(current->configure)) {
      ApplyLegacyBackground(current->camera, sawBackgroundMode);
      presets.push_back(std::move(*current));
    }
    current.reset();
  };
  while (!text.empty()) {
    const size_t eol = text.find('\n');
    std::string_view line = text.substr(0, eol);
    text = eol == std::string_view::npos ? std::string_view() : text.substr(eol + 1);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
      flush();
      current = Preset{};
      current->name = std::string(line.substr(1, line.size() - 2));
      current->configure = proto::Configure{0, 0, 0, 0};
      broken = ValidatePresetName(current->name, {}, false) != PresetNameError::None;
      sawBackgroundMode = false;
      continue;
    }
    const size_t eq = line.find('=');
    if (!current || eq == std::string_view::npos) continue;
    const auto key = line.substr(0, eq);
    const auto value = line.substr(eq + 1);
    if (key == "width" || key == "height" || key == "fps" || key == "bitrate_kbps") {
      broken |= !ParseConfigureKey(key, value, current->configure);
    } else if (key == "background_image") {
      if (!value.empty()) current->backgroundImage = std::string(value);
    } else if (ParseCameraKey(key, value, current->camera)) {
      sawBackgroundMode |= key == "background_mode";
    }
  }
  flush();
  return presets;
}

bool SameNameIgnoreCase(std::string_view a, std::string_view b) {
  const auto wa = Wide(a);
  const auto wb = Wide(b);
  return CompareStringEx(LOCALE_NAME_INVARIANT, LINGUISTIC_IGNORECASE, wa.data(), int(wa.size()), wb.data(),
                         int(wb.size()), nullptr, nullptr, 0) == CSTR_EQUAL;
}

PresetNameError ValidatePresetName(std::string_view name, const std::vector<Preset>& user, bool allowOverwrite) {
  if (name.empty()) return PresetNameError::Empty;
  if (CodePoints(name) > 32) return PresetNameError::TooLong;
  if (name.find_first_of("[]\r\n") != std::string_view::npos) return PresetNameError::BadChar;
  for (const auto& p : BuiltinPresets()) {
    if (SameNameIgnoreCase(name, p.name)) return PresetNameError::Builtin;
  }
  if (!allowOverwrite) {
    for (const auto& p : user) {
      if (SameNameIgnoreCase(name, p.name)) return PresetNameError::Exists;
    }
  }
  return PresetNameError::None;
}

std::optional<proto::Configure> MatchFormat(const proto::Configure& wanted,
                                            const std::vector<proto::CaptureFormat>& phone) {
  std::optional<proto::Configure> best;
  for (const auto& f : phone) {
    if (f.width != wanted.width || f.height != wanted.height) continue;
    const uint8_t fps = std::min(wanted.fps, f.maxFps);
    if (!best || fps > best->fps) best = proto::Configure{wanted.width, wanted.height, fps, wanted.bitrateKbps};
  }
  return best;
}

bool PresetMatches(const Preset& preset, const proto::Configure& configure, const proto::CameraSettings& camera,
                   std::string_view backgroundImage) {
  return preset.configure == configure && preset.camera == camera && preset.backgroundImage == backgroundImage;
}

std::filesystem::path DefaultPresetsPath() { return DefaultSettingsPath().parent_path() / L"presets.ini"; }

std::vector<Preset> LoadPresets(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return {};
  const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  return ParsePresets(text);
}

bool SavePresets(const std::filesystem::path& path, const std::vector<Preset>& user) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  const auto temp = path.wstring() + L".tmp";
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    const auto text = SerializePresets(user);
    if (!file.write(text.data(), std::streamsize(text.size()))) return false;
  }
  return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

}  // namespace webicam
