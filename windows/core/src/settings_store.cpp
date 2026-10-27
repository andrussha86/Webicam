#include "webicam/settings_store.h"

#include <windows.h>

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace webicam {

namespace {

template <typename T>
bool ParseNumber(std::string_view text, T& out) {
  T value{};
  const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc() || end != text.data() + text.size()) return false;
  out = value;
  return true;
}

// Shortest text that reads back as the same float.
std::string F(float v) {
  char buf[32];
  const auto [end, ec] = std::to_chars(buf, buf + sizeof(buf), v);
  return std::string(buf, end);
}

bool ParseBool(std::string_view text, bool& out) {
  if (text != "0" && text != "1") return false;
  out = text == "1";
  return true;
}

}  // namespace

void AppendConfigureKeys(std::string& out, const proto::Configure& c) {
  out += "width=" + std::to_string(c.width) + "\n";
  out += "height=" + std::to_string(c.height) + "\n";
  out += "fps=" + std::to_string(c.fps) + "\n";
  out += "bitrate_kbps=" + std::to_string(c.bitrateKbps) + "\n";
}

bool ParseConfigureKey(std::string_view key, std::string_view value, proto::Configure& c) {
  unsigned u = 0;
  if (key == "width") return ParseNumber(value, c.width);
  if (key == "height") return ParseNumber(value, c.height);
  if (key == "fps") return ParseNumber(value, u) && u <= 255 && (c.fps = uint8_t(u), true);
  if (key == "bitrate_kbps") return ParseNumber(value, c.bitrateKbps);
  return false;
}

void AppendCameraKeys(std::string& out, const proto::CameraSettings& c) {
  char color[8];
  std::snprintf(color, sizeof(color), "%06X", unsigned(c.backgroundColor));
  out += "lens=" + std::to_string(unsigned(c.lens)) + "\n";
  out += "zoom=" + F(c.zoom) + "\n";
  out += "manual_exposure=" + std::to_string(c.manualExposure) + "\n";
  out += "iso=" + F(c.iso) + "\n";
  out += "shutter_us=" + std::to_string(c.shutterUs) + "\n";
  out += "exposure_bias=" + F(c.exposureBias) + "\n";
  out += "manual_wb=" + std::to_string(c.manualWhiteBalance) + "\n";
  out += "temperature_k=" + F(c.temperatureK) + "\n";
  out += "tint=" + F(c.tint) + "\n";
  out += "manual_focus=" + std::to_string(c.manualFocus) + "\n";
  out += "lens_position=" + F(c.lensPosition) + "\n";
  out += "blur=" + std::to_string(unsigned(c.blur)) + "\n";
  out += "rotate180=" + std::to_string(c.rotate180) + "\n";
  out += "face_focus=" + std::to_string(c.faceFocus) + "\n";
  out += "background_mode=" + std::to_string(unsigned(c.backgroundMode)) + "\n";
  out += std::string("background_color=") + color + "\n";
}

bool ParseCameraKey(std::string_view key, std::string_view value, proto::CameraSettings& c) {
  unsigned u = 0;
  if (key == "lens") return ParseNumber(value, u) && u < unsigned(proto::kLensCount) && (c.lens = proto::Lens(u), true);
  if (key == "zoom") return ParseNumber(value, c.zoom);
  if (key == "manual_exposure") return ParseBool(value, c.manualExposure);
  if (key == "iso") return ParseNumber(value, c.iso);
  if (key == "shutter_us") return ParseNumber(value, c.shutterUs);
  if (key == "exposure_bias") return ParseNumber(value, c.exposureBias);
  if (key == "manual_wb") return ParseBool(value, c.manualWhiteBalance);
  if (key == "temperature_k") return ParseNumber(value, c.temperatureK);
  if (key == "tint") return ParseNumber(value, c.tint);
  if (key == "manual_focus") return ParseBool(value, c.manualFocus);
  if (key == "lens_position") return ParseNumber(value, c.lensPosition);
  if (key == "blur") return ParseNumber(value, u) && u <= 100 && (c.blur = uint8_t(u), true);
  if (key == "rotate180") return ParseBool(value, c.rotate180);
  if (key == "face_focus") return ParseBool(value, c.faceFocus);
  if (key == "background_mode") {
    return ParseNumber(value, u) && u < unsigned(proto::kBackgroundModeCount) &&
           (c.backgroundMode = proto::BackgroundMode(u), true);
  }
  if (key == "background_color") {
    uint32_t color = 0;
    const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), color, 16);
    if (value.size() != 6 || ec != std::errc() || end != value.data() + value.size()) return false;
    c.backgroundColor = color;
    return true;
  }
  return false;
}

void ApplyLegacyBackground(proto::CameraSettings& c, bool sawBackgroundMode) {
  if (!sawBackgroundMode) c.backgroundMode = c.blur > 0 ? proto::BackgroundMode::Blur : proto::BackgroundMode::Off;
}

std::string SerializeSettings(const StoredSettings& s) {
  std::string out;
  if (s.configure) AppendConfigureKeys(out, *s.configure);
  AppendCameraKeys(out, s.camera);
  out += "background_image=" + s.backgroundImage + "\n";
  if (!s.preset.empty()) out += "preset=" + s.preset + "\n";
  return out;
}

StoredSettings ParseSettings(std::string_view text) {
  StoredSettings s;
  proto::Configure configure{0, 0, 0, 0};
  bool sawBackgroundMode = false;
  while (!text.empty()) {
    const size_t eol = text.find('\n');
    std::string_view line = text.substr(0, eol);
    text = eol == std::string_view::npos ? std::string_view() : text.substr(eol + 1);
    if (!line.empty() && line.back() == '') line.remove_suffix(1);
    const size_t eq = line.find('=');
    if (eq == std::string_view::npos) continue;
    const auto key = line.substr(0, eq);
    const auto value = line.substr(eq + 1);
    if (ParseConfigureKey(key, value, configure)) continue;
    if (ParseCameraKey(key, value, s.camera)) {
      sawBackgroundMode |= key == "background_mode";
    } else if (key == "background_image" && !value.empty()) {
      s.backgroundImage = std::string(value);
    } else if (key == "preset") {
      s.preset = std::string(value);
    }
  }
  ApplyLegacyBackground(s.camera, sawBackgroundMode);
  if (configure.width && configure.height && configure.fps && configure.bitrateKbps) s.configure = configure;
  return s;
}

std::filesystem::path DefaultSettingsPath() {
  wchar_t* base = nullptr;
  size_t length = 0;
  std::filesystem::path dir;
  if (_wdupenv_s(&base, &length, L"LOCALAPPDATA") == 0 && base) dir = base;
  std::free(base);
  return dir / L"Webicam" / L"settings.ini";
}

StoredSettings LoadSettings(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return {};
  std::ostringstream text;
  text << file.rdbuf();
  return ParseSettings(text.str());
}

bool SaveSettings(const std::filesystem::path& path, const StoredSettings& settings) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  const auto temp = path.wstring() + L".tmp";
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file << SerializeSettings(settings);
    if (!file.good()) return false;
  }
  return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

}  // namespace webicam
