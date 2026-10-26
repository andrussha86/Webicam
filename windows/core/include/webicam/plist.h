#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace webicam {

// Minimal XML property list: just what usbmuxd speaks.
struct PlistValue {
  enum class Kind { Integer, String, Dict, Array };

  static PlistValue Int(int64_t v) { return {Kind::Integer, v, {}, {}, {}}; }
  static PlistValue Str(std::string v) { return {Kind::String, 0, std::move(v), {}, {}}; }
  static PlistValue Dict(std::vector<std::pair<std::string, PlistValue>> v) { return {Kind::Dict, 0, {}, std::move(v), {}}; }
  static PlistValue Array(std::vector<PlistValue> v) { return {Kind::Array, 0, {}, {}, std::move(v)}; }

  const PlistValue* Find(std::string_view key) const;
  bool operator==(const PlistValue&) const = default;

  Kind kind = Kind::String;
  int64_t integer = 0;
  std::string str;
  std::vector<std::pair<std::string, PlistValue>> dict;
  std::vector<PlistValue> array;
};

std::string ToXml(const PlistValue& value);
std::optional<PlistValue> ParseXml(std::string_view xml);

}  // namespace webicam
