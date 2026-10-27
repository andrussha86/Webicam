#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "file_dialog.h"
#include "thumbnails.h"
#include "webicam/audio_output.h"
#include "webicam/background_library.h"
#include "webicam/connection_manager.h"
#include "webicam/pipeline.h"
#include "webicam/presets.h"

namespace webicam::app {

// Sidebar tabs: how the phone is reached, what the picture is (format, lens, zoom, orientation),
// how the camera exposes it, what is behind the person, where the phone's microphone goes.
enum class Tab { Connection = 0, Frame = 1, Camera = 2, Background = 3, Sound = 4 };
inline constexpr int kTabCount = 5;

struct UiState {
  Tab tab = Tab::Connection;
  // The Connection tab was opened by the app (not the user) and closes itself once streaming starts.
  bool connectionTabAuto = true;
  std::optional<ConnState> lastConnState;
  char codeInput[8] = "";      // code from the phone's screen, digits only
  std::string codeInputFor;    // phone id the field was filled for
  char addressInput[96] = "";  // typed Wi-Fi address being edited
  std::string wifiAddress;     // last applied typed address (from settings at start)
  bool addressShown = false;   // «Не находит iPhone?» opened
  std::string audioDevice;     // chosen playback endpoint; "" = the virtual cable, if installed
  std::vector<AudioDevice> audioDevices;
  uint64_t audioScanMs = 0;    // when audioDevices was last read
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
  AudioOutput& audio;
};

// Draws the whole window with Dear ImGui.
void DrawUi(UiState& state, ConnectionManager& connection, Pipeline& pipeline, UiServices& services);

// Sections in their own files.
// Full-width bar above the sidebar and preview: name, link status, the phone's link, battery and heat.
void DrawTopBar(UiState& state, ConnectionManager& connection, UiServices& services);
void DrawConnectionSection(UiState& state, ConnectionManager& connection);
void DrawSoundSection(UiState& state, ConnectionManager& connection, UiServices& services);
// Rescans playback devices now and then and points the player at the chosen one (none while the
// microphone is off); every frame.
void UpdateAudioOutput(UiState& state, ConnectionManager& connection, UiServices& services);
// The device the phone's sound goes to: the chosen one, or the virtual cable; "" when there is none.
std::string EffectiveAudioDevice(const UiState& state);
void DrawPresetSection(UiState& state, ConnectionManager& connection, UiServices& services);
void DrawBackgroundSection(UiState& state, ConnectionManager& connection, UiServices& services);
// Applies a picture added through the «+» tile once the dialog and the copy are done; every frame.
void PollImagePicker(UiState& state, UiServices& services);

// Keeps the zoomed area inside the frame.
void ClampPan(proto::CameraSettings& camera);

}  // namespace webicam::app
