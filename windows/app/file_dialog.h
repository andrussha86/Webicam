#pragma once

#include <windows.h>

#include <filesystem>
#include <optional>

namespace webicam::app {

// System «Open» dialog filtered to pictures; nullopt when cancelled.
std::optional<std::filesystem::path> PickImageFile(HWND owner);

}  // namespace webicam::app
