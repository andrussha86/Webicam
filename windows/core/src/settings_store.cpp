#include "webicam/settings_store.h"

#include <windows.h>

#include <charconv>
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

std::string SerializeSettings(const StoredSettings& s) {
  std::ostringstream out;
  if (s.configure) {
    out << "width=" << s.configure->width << "\n"
        << "height=" << s.configure->height << "\n"
        << "fps=" << unsigned(s.configure->fps) << "\n"
        << "bitrate_kbps=" << s.configure->bitrateKbps << "\n";
  }
  const auto& c = s.camera;
  out << "lens=" << unsigned(c.lens) << "\n"
      << "zoom=" << F(c.zoom) << "\n"
      << "manual_exposure=" << c.manualExposure << "\n"
      << "iso=" << F(c.iso) << "\n"
      << "shutter_us=" << c.shutterUs << "\n"
      << "exposure_bias=" << F(c.exposureBias) << "\n"
      << "manual_wb=" << c.manualWhiteBalance << "\n"
      << "temperature_k=" << F(c.temperatureK) << "\n"
      << "tint=" << F(c.tint) << "\n"
      << "manual_focus=" << c.manualFocus << "\n"
      << "lens_position=" << F(c.lensPosition) << "\n"
      << "blur=" << unsigned(c.blur) << "\n"
      << "rotate180=" << c.rotate180 << "\n";
  return out.str();
}

StoredSettings ParseSettings(std::string_view text) {
  StoredSettings s;
  proto::Configure configure{0, 0, 0, 0};
  auto& c = s.camera;
  while (!text.empty()) {
    const size_t eol = text.find('\n');
    std::string_view line = text.substr(0, eol);
    text = eol == std::string_view::npos ? std::string_view() : text.substr(eol + 1);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    const size_t eq = line.find('=');
    if (eq == std::string_view::npos) continue;
    const auto key = line.substr(0, eq);
    const auto value = line.substr(eq + 1);
    unsigned u = 0;
    if (key == "width") ParseNumber(value, configure.width);
    else if (key == "height") ParseNumber(value, configure.height);
    else if (key == "fps" && ParseNumber(value, u) && u <= 255) configure.fps = uint8_t(u);
    else if (key == "bitrate_kbps") ParseNumber(value, configure.bitrateKbps);
    else if (key == "lens" && ParseNumber(value, u) && u < unsigned(proto::kLensCount)) c.lens = proto::Lens(u);
    else if (key == "zoom") ParseNumber(value, c.zoom);
    else if (key == "manual_exposure") ParseBool(value, c.manualExposure);
    else if (key == "iso") ParseNumber(value, c.iso);
    else if (key == "shutter_us") ParseNumber(value, c.shutterUs);
    else if (key == "exposure_bias") ParseNumber(value, c.exposureBias);
    else if (key == "manual_wb") ParseBool(value, c.manualWhiteBalance);
    else if (key == "temperature_k") ParseNumber(value, c.temperatureK);
    else if (key == "tint") ParseNumber(value, c.tint);
    else if (key == "manual_focus") ParseBool(value, c.manualFocus);
    else if (key == "lens_position") ParseNumber(value, c.lensPosition);
    else if (key == "blur" && ParseNumber(value, u) && u <= 100) c.blur = uint8_t(u);
    else if (key == "rotate180") ParseBool(value, c.rotate180);
  }
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
