#pragma once

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
};

// Draws the whole window with Dear ImGui.
void DrawUi(UiState& state, ConnectionManager& connection, Pipeline& pipeline);

}  // namespace webicam::app
