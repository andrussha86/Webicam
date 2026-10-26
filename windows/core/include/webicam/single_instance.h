#pragma once

#include <windows.h>

#include <optional>

namespace webicam {

inline constexpr wchar_t kSingleInstanceName[] = L"Local\\WebicamApp-SingleInstance";

// Named mutex; nullopt if another holder exists. Close the handle to release.
std::optional<HANDLE> AcquireSingleInstance(const wchar_t* name = kSingleInstanceName);

}  // namespace webicam
