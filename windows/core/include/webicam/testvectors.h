#pragma once

#include <vector>

#include "webicam/protocol.h"

namespace webicam::proto {

struct TestVector {
  const char* file;
  Message message;
};

// Expected values of shared/testvectors/*.bin. Mirrored in ios/WebicamCore tests.
inline std::vector<TestVector> TestVectors() {
  return {
      {"hello_pc.bin", Hello{2, "", {}}},
      {"hello_phone.bin", Hello{2, "iPhone16,1", {{1280, 720, 60}, {1920, 1080, 60}, {3840, 2160, 30}}}},
      {"configure_1080p60.bin", Configure{1920, 1080, 60, 25000}},
      {"video_frame_key.bin", VideoFrame{123456789, true, {0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1F}}},
      {"request_keyframe.bin", RequestKeyframe{}},
      {"ping.bin", Ping{1000}},
      {"pong.bin", Pong{1000, 2000}},
      {"status_thermal.bin", Status{StreamState::ThermalThrottled, 2, "serious"}},
      {"camera_settings.bin",
       CameraSettings{Lens::Telephoto, 1.5f, true, 400.0f, 8000, -0.5f, true, 4500.0f, 10.0f, true, 0.25f, 60, true}},
      {"camera_state.bin",
       CameraState{0x1F, Lens::Wide, 50.0f, 3200.0f, 20, 16666, -8.0f, 8.0f, 10.0f, true, 200.0f, 10000, 5200.0f,
                   -5.0f, 0.75f}},
  };
}

}  // namespace webicam::proto
