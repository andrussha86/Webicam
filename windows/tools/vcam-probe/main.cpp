// Risk check for the virtual camera without a phone: registers the camera,
// waits for a consumer to open it, then publishes moving color bars.
//   vcam-probe [--stop-heartbeat-after <seconds>]
#include <windows.h>
#include <mfapi.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "webicam/frame_ring.h"
#include "webicam/shared_section.h"
#include "webicam/vcam_logic.h"
#include "webicam/virtual_cam_host.h"

using namespace webicam;

namespace {

// Eight vertical bars (Y, U, V), scrolled horizontally by `offset`.
void DrawBars(std::vector<uint8_t>& nv12, uint32_t width, uint32_t height, uint32_t offset) {
  static constexpr uint8_t kBars[8][3] = {{235, 128, 128}, {210, 16, 146}, {170, 166, 16}, {145, 54, 34},
                                          {106, 202, 222}, {81, 90, 240},  {41, 240, 110}, {16, 128, 128}};
  nv12.resize(size_t(width) * height * 3 / 2);
  uint8_t* y = nv12.data();
  uint8_t* uv = y + size_t(width) * height;
  for (uint32_t x = 0; x < width; ++x) {
    const auto& bar = kBars[((x + offset) % width) * 8 / width];
    for (uint32_t row = 0; row < height; ++row) y[size_t(row) * width + x] = bar[0];
    if (x % 2 == 0) {
      for (uint32_t row = 0; row < height / 2; ++row) {
        uv[size_t(row) * width + x] = bar[1];
        uv[size_t(row) * width + x + 1] = bar[2];
      }
    }
  }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  int stopHeartbeatAfter = -1;
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::wstring(argv[i]) == L"--stop-heartbeat-after") stopHeartbeatAfter = _wtoi(argv[i + 1]);
  }

  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  MFStartup(MF_VERSION);
  VirtualCamHost host;
  if (!host.Start()) {
    std::fwprintf(stderr, L"virtual camera: %s\n", host.LastError().c_str());
    return 1;
  }
  std::printf("virtual camera 'Webicam' started; open it in the Camera app or OBS. Ctrl+C to quit.\n");

  HANDLE frameEvent = nullptr;
  std::optional<SharedSection> section;
  std::optional<FrameRing> ring;
  std::vector<uint8_t> frame;
  const auto start = std::chrono::steady_clock::now();
  bool announced = false;
  for (uint32_t n = 0;; ++n) {
    std::this_thread::sleep_for(std::chrono::milliseconds(33));
    if (!ring) {
      if (n % 15 == 0) {
        section = SharedSection::Open(shm::kSectionName, FrameRing::RequiredBytes());
        if (section) ring = FrameRing::Attach(section->data());
        if (!ring) {
          section.reset();
          if (!announced) std::printf("waiting for section (no consumer yet)...\n");
          announced = true;
          continue;
        }
        frameEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, shm::kFrameEventName);
        std::printf("attached to shared memory%s\n", frameEvent ? "" : " (no frame event)");
      } else {
        continue;
      }
    }
    const auto format = ring->RequestedFormat();
    const uint32_t width = format ? (*format)[0] : 1920;
    const uint32_t height = format ? (*format)[1] : 1080;
    DrawBars(frame, width, height, n * 8);
    ring->Publish({width, height, width, int64_t(n)}, frame);
    if (frameEvent) SetEvent(frameEvent);
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start);
    if (stopHeartbeatAfter < 0 || elapsed.count() < stopHeartbeatAfter) ring->SetHeartbeat(SteadyNowMs(), true);
  }
}
