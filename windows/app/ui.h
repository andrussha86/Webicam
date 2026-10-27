#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "file_dialog.h"
#include "thumbnails.h"
#include "webicam/background_library.h"
#include "webicam/connection_manager.h"
#include "webicam/pipeline.h"
#include "webicam/presets.h"

namespace webicam::app {

// Sidebar tabs: what the picture is (format, lens, zoom, orientation), how the camera exposes it,
// what is behind the person.
enum class Tab { Frame = 0, Camera = 1, Background = 2 };
inline constexpr int kTabCount = 3;

struct UiState {
  Tab tab = Tab::Frame;
  float bitrateMbps = 0;
  bool bitrateDragging = false;
  std::string cameraError;  // virtual camera registration problem, if any
  std::string phoneStatus;  // last Status message from the phone
  std::optional<proto::Configure> chosenConfigure;  // last applied by the user; saved to settings
  proto::CameraSettings camera;                     // edited here, sent to the phone throttled
  uint64_t lastCameraSendMs = 0;

  std::vector<Preset> userPresets;
  std::string activePreset;                   // last applied or saved; "" = none
  std::string backgroundImage = "builtin:0";  // "builtin:N" or a file in the backgrounds folder
  std::string presetHint;                     // shown under the preset section
  bool presetHintIsError = false;
  std::string backgroundError;  // add/remove picture failed
  ImagePicker picker;           // «+» tile: open dialog, then the picture is stored off the UI thread
};

// Things the sidebar needs besides the connection.
struct UiServices {
  HWND window = nullptr;
  BackgroundLibrary& library;
  ThumbnailCache& thumbnails;
  // Records the picture the phone should get and resends it if it is on screen.
  std::function<void(const std::string& ref)> setBackground;
};

// Draws the whole window with Dear ImGui.
void DrawUi(UiState& state, ConnectionManager& connection, Pipeline& pipeline, UiServices& services);

// Sections in their own files.
void DrawPresetSection(UiState& state, ConnectionManager& connection, UiServices& services);
void DrawBackgroundSection(UiState& state, ConnectionManager& connection, UiServices& services);
// Applies a picture added through the «+» tile once the dialog and the copy are done; every frame.
void PollImagePicker(UiState& state, UiServices& services);

// Keeps the zoomed area inside the frame.
void ClampPan(proto::CameraSettings& camera);

}  // namespace webicam::app
