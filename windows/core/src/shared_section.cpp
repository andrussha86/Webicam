#include "webicam/shared_section.h"

#include <windows.h>
#include <sddl.h>

#include <utility>

namespace webicam {

std::optional<SharedSection> SharedSection::Create(const wchar_t* name, size_t bytes, const wchar_t* sddl) {
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &descriptor, nullptr)) {
    return std::nullopt;
  }
  SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
  HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &attributes, PAGE_READWRITE,
                                      DWORD(uint64_t(bytes) >> 32), DWORD(bytes & 0xFFFFFFFF), name);
  const bool existed = GetLastError() == ERROR_ALREADY_EXISTS;
  LocalFree(descriptor);
  if (!mapping) return std::nullopt;
  void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, bytes);
  if (!view) {
    CloseHandle(mapping);
    return std::nullopt;
  }
  return SharedSection(mapping, view, existed);
}

std::optional<SharedSection> SharedSection::Open(const wchar_t* name, size_t bytes) {
  HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
  if (!mapping) return std::nullopt;
  void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, bytes);
  if (!view) {
    CloseHandle(mapping);
    return std::nullopt;
  }
  return SharedSection(mapping, view, true);
}

SharedSection::SharedSection(SharedSection&& other) noexcept
    : mapping_(std::exchange(other.mapping_, nullptr)),
      view_(std::exchange(other.view_, nullptr)),
      existed_(other.existed_) {}

SharedSection& SharedSection::operator=(SharedSection&& other) noexcept {
  if (this != &other) {
    Reset();
    mapping_ = std::exchange(other.mapping_, nullptr);
    view_ = std::exchange(other.view_, nullptr);
    existed_ = other.existed_;
  }
  return *this;
}

SharedSection::~SharedSection() { Reset(); }

void SharedSection::Reset() {
  if (view_) UnmapViewOfFile(view_);
  if (mapping_) CloseHandle(mapping_);
  view_ = nullptr;
  mapping_ = nullptr;
}

}  // namespace webicam
