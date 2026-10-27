#include "webicam/background_library.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <iterator>

#include "webicam/image_codec.h"
#include "webicam/settings_store.h"

namespace webicam {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kBuiltinPrefix = "builtin:";

std::optional<int> BuiltinIndex(std::string_view ref) {
  if (!ref.starts_with(kBuiltinPrefix)) return std::nullopt;
  ref.remove_prefix(kBuiltinPrefix.size());
  int index = -1;
  const auto [end, ec] = std::from_chars(ref.data(), ref.data() + ref.size(), index);
  if (ec != std::errc() || end != ref.data() + ref.size() || index < 0 || index >= kBuiltinBackgroundCount) {
    return std::nullopt;
  }
  return index;
}

// Library names are plain file names; anything with a path part is not ours.
bool IsPlainName(std::string_view name) {
  return !name.empty() && name.find_first_of("/\\:") == std::string_view::npos && name != "." && name != "..";
}

std::string LowerAsciiExtension(const fs::path& path) {
  std::string ext;
  for (wchar_t c : path.extension().wstring()) {
    if (c > 0x7F) return ".img";
    ext += char(c >= L'A' && c <= L'Z' ? c - L'A' + L'a' : c);
  }
  return ext.empty() ? ".img" : ext;
}

}  // namespace

BackgroundLibrary::BackgroundLibrary(fs::path dir) : dir_(std::move(dir)) {}

std::vector<std::string> BackgroundLibrary::List() const {
  std::vector<std::pair<fs::file_time_type, std::string>> files;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(dir_, ec)) {
    if (!entry.is_regular_file(ec) || entry.path().extension() == L".tmp") continue;
    files.emplace_back(entry.last_write_time(ec), entry.path().filename().string());
  }
  std::sort(files.begin(), files.end());
  std::vector<std::string> names;
  for (auto& [time, name] : files) names.push_back(std::move(name));
  return names;
}

std::expected<std::string, std::string> BackgroundLibrary::Add(const fs::path& source) {
  const std::string failure = "Не удалось открыть картинку: " + source.filename().string();
  if (!LoadImageFile(source)) return std::unexpected(failure);
  std::ifstream file(source, std::ios::binary);
  const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (bytes.empty()) return std::unexpected(failure);
  uint64_t hash = 14695981039346656037ull;  // FNV-1a 64
  for (char b : bytes) hash = (hash ^ uint8_t(b)) * 1099511628211ull;
  char hex[17];
  std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(hash));
  const std::string name = hex + LowerAsciiExtension(source);
  const fs::path target = dir_ / name;
  std::error_code ec;
  if (fs::exists(target, ec)) return name;
  fs::create_directories(dir_, ec);
  if (!fs::copy_file(source, target, ec)) return std::unexpected("Не удалось сохранить картинку");
  return name;
}

std::expected<void, std::string> BackgroundLibrary::Remove(const std::string& name, const std::vector<Preset>& presets) {
  for (const auto& p : presets) {
    if (p.backgroundImage == name) return std::unexpected("Используется в пресете „" + p.name + "“");
  }
  if (!IsPlainName(name)) return std::unexpected("Не удалось удалить картинку");
  std::error_code ec;
  fs::remove(dir_ / name, ec);
  if (ec) return std::unexpected("Не удалось удалить картинку");
  return {};
}

std::optional<Bgra> BackgroundLibrary::Resolve(std::string_view ref, uint32_t width, uint32_t height) const {
  if (const auto index = BuiltinIndex(ref)) return RenderBuiltinBackground(*index, width, height);
  if (ref.starts_with(kBuiltinPrefix) || !IsPlainName(ref)) return std::nullopt;
  const auto image = LoadImageFile(dir_ / fs::path(std::string(ref)));
  if (!image) return std::nullopt;
  auto scaled = CoverResize(*image, width, height);
  if (scaled.pixels.empty()) return std::nullopt;
  return scaled;
}

fs::path DefaultBackgroundsDir() { return DefaultSettingsPath().parent_path() / L"backgrounds"; }

}  // namespace webicam
