#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "webicam/connection_manager.h"
#include "webicam/pipeline.h"

namespace webicam::app {

struct UiState {
  int formatIndex = -1;  // index into the capture choices; -1 = follow the connection's current config
  float bitrateMbps = 0;
  std::string cameraError;  // virtual camera registration problem, if any
  std::string phoneStatus;  // last Status message from the phone
  std::optional<proto::Configure> chosenConfigure;  // last applied by the user; saved to settings
  proto::CameraSettings camera;                     // edited here, sent to the phone throttled
  uint64_t lastCameraSendMs = 0;
};

// Draws the whole window with Dear ImGui.
void DrawUi(UiState& state, ConnectionManager& connection, Pipeline& pipeline);

}  // namespace webicam::app
