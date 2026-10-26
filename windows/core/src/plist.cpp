#include "webicam/plist.h"

#include <charconv>

namespace webicam {

const PlistValue* PlistValue::Find(std::string_view key) const {
  for (const auto& [k, v] : dict) {
    if (k == key) return &v;
  }
  return nullptr;
}

namespace {

std::string Escape(std::string_view s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      default: out += c;
    }
  }
  return out;
}

std::string Unescape(std::string_view s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '&') {
      if (s.substr(i, 5) == "&amp;") { out += '&'; i += 4; continue; }
      if (s.substr(i, 4) == "&lt;") { out += '<'; i += 3; continue; }
      if (s.substr(i, 4) == "&gt;") { out += '>'; i += 3; continue; }
    }
    out += s[i];
  }
  return out;
}

void Write(const PlistValue& v, std::string& out) {
  switch (v.kind) {
    case PlistValue::Kind::Integer: out += "<integer>" + std::to_string(v.integer) + "</integer>"; break;
    case PlistValue::Kind::String: out += "<string>" + Escape(v.str) + "</string>"; break;
    case PlistValue::Kind::Dict:
      out += "<dict>";
      for (const auto& [k, child] : v.dict) {
        out += "<key>" + Escape(k) + "</key>";
        Write(child, out);
      }
      out += "</dict>";
      break;
    case PlistValue::Kind::Array:
      out += "<array>";
      for (const auto& child : v.array) Write(child, out);
      out += "</array>";
      break;
  }
}

class Parser {
 public:
  explicit Parser(std::string_view xml) : s_(xml) {}

  std::optional<PlistValue> Document() {
    auto start = s_.find("<plist");
    if (start == std::string_view::npos) return std::nullopt;
    pos_ = s_.find('>', start);
    if (pos_ == std::string_view::npos) return std::nullopt;
    ++pos_;
    return Value();
  }

 private:
  void SkipSpace() {
    while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\n' || s_[pos_] == '\r' || s_[pos_] == '\t')) ++pos_;
  }

  std::optional<std::string_view> Tag() {
    SkipSpace();
    if (pos_ >= s_.size() || s_[pos_] != '<') return std::nullopt;
    auto end = s_.find('>', pos_);
    if (end == std::string_view::npos) return std::nullopt;
    auto tag = s_.substr(pos_ + 1, end - pos_ - 1);
    pos_ = end + 1;
    return tag;
  }

  std::optional<std::string_view> TextUntil(std::string_view closing) {
    auto end = s_.find(closing, pos_);
    if (end == std::string_view::npos) return std::nullopt;
    auto text = s_.substr(pos_, end - pos_);
    pos_ = end + closing.size();
    return text;
  }

  std::optional<PlistValue> Value() {
    auto tag = Tag();
    if (!tag) return std::nullopt;
    if (*tag == "integer") {
      auto text = TextUntil("</integer>");
      if (!text) return std::nullopt;
      int64_t v = 0;
      auto [ptr, ec] = std::from_chars(text->data(), text->data() + text->size(), v);
      if (ec != std::errc{} || ptr != text->data() + text->size()) return std::nullopt;
      return PlistValue::Int(v);
    }
    if (*tag == "string") {
      auto text = TextUntil("</string>");
      if (!text) return std::nullopt;
      return PlistValue::Str(Unescape(*text));
    }
    if (*tag == "string/") return PlistValue::Str("");
    if (*tag == "true/") return PlistValue::Int(1);
    if (*tag == "false/") return PlistValue::Int(0);
    if (*tag == "dict/") return PlistValue::Dict({});
    if (*tag == "array/") return PlistValue::Array({});
    if (*tag == "dict") {
      std::vector<std::pair<std::string, PlistValue>> entries;
      while (true) {
        auto save = pos_;
        auto next = Tag();
        if (!next) return std::nullopt;
        if (*next == "/dict") break;
        if (*next != "key") return std::nullopt;
        auto key = TextUntil("</key>");
        if (!key) return std::nullopt;
        auto value = Value();
        if (!value) return std::nullopt;
        entries.emplace_back(Unescape(*key), std::move(*value));
        (void)save;
      }
      return PlistValue::Dict(std::move(entries));
    }
    if (*tag == "array") {
      std::vector<PlistValue> items;
      while (true) {
        auto save = pos_;
        auto next = Tag();
        if (!next) return std::nullopt;
        if (*next == "/array") break;
        pos_ = save;
        auto value = Value();
        if (!value) return std::nullopt;
        items.push_back(std::move(*value));
      }
      return PlistValue::Array(std::move(items));
    }
    // Unsupported types (data, date, real) are skipped as empty strings.
    if (tag->back() == '/') return PlistValue::Str("");
    auto closing = "</" + std::string(*tag) + ">";
    if (!TextUntil(closing)) return std::nullopt;
    return PlistValue::Str("");
  }

  std::string_view s_;
  size_t pos_ = 0;
};

}  // namespace

std::string ToXml(const PlistValue& value) {
  std::string out =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
      "<plist version=\"1.0\">";
  Write(value, out);
  out += "</plist>\n";
  return out;
}

std::optional<PlistValue> ParseXml(std::string_view xml) { return Parser(xml).Document(); }

}  // namespace webicam
