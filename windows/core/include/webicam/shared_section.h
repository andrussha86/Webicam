#pragma once

#include <cstddef>
#include <optional>

namespace webicam {

// RAII wrapper around a named file mapping.
class SharedSection {
 public:
  static std::optional<SharedSection> Create(const wchar_t* name, size_t bytes, const wchar_t* sddl);
  static std::optional<SharedSection> Open(const wchar_t* name, size_t bytes);

  SharedSection(SharedSection&& other) noexcept;
  SharedSection& operator=(SharedSection&& other) noexcept;
  SharedSection(const SharedSection&) = delete;
  SharedSection& operator=(const SharedSection&) = delete;
  ~SharedSection();

  void* data() const { return view_; }
  // True if Create() found an existing section instead of making a new one.
  bool existed() const { return existed_; }

 private:
  SharedSection(void* mapping, void* view, bool existed) : mapping_(mapping), view_(view), existed_(existed) {}
  void Reset();

  void* mapping_ = nullptr;
  void* view_ = nullptr;
  bool existed_ = false;
};

}  // namespace webicam
