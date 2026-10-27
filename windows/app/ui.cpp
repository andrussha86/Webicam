#include "ui.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "webicam/vcam_logic.h"

namespace webicam::app {

namespace {

struct Choice {
  proto::Configure configure;
  std::string label;
};

// Every phone format at 30 fps and, where supported, 60 fps.
std::vector<Choice> CaptureChoices(const std::vector<proto::CaptureFormat>& formats) {
  std::vector<Choice> choices;
  for (const auto& f : formats) {
    for (uint8_t fps : {uint8_t(30), uint8_t(60)}) {
      if (fps > f.maxFps) continue;
      char label[64];
      std::snprintf(label, sizeof(label), "%u×%u @ %u", f.width, f.height, fps);
      choices.push_back({{f.width, f.height, fps, proto::DefaultBitrateKbps(f.height, fps)}, label});
    }
  }
  return choices;
}

constexpr uint64_t kCameraSendIntervalMs = 50;  // at most 20 CameraSettings per second

// Ranges to use before the phone has reported its own.
proto::CameraState FallbackCameraState() {
  proto::CameraState s;
  s.lenses = 0x1F;
  s.isoMin = 25;
  s.isoMax = 3200;
  s.shutterMinUs = 50;
  s.shutterMaxUs = 33333;
  s.biasMin = -8;
  s.biasMax = 8;
  s.zoomMax = 5;
  s.manualFocusSupported = true;
  s.iso = 100;
  s.shutterUs = 10000;
  s.temperatureK = 5000;
  s.lensPosition = 0.5f;
  return s;
}

// "Авто" checkbox in front of a group: returns true while the group is manual.
bool ManualToggle(const char* id, bool& manual) {
  bool automatic = !manual;
  if (ImGui::Checkbox(id, &automatic)) manual = !automatic;
  return manual;
}

void DrawCameraControls(UiState& state, ConnectionManager& connection) {
  const auto phone = connection.PhoneCamera();
  const proto::CameraState range = phone.value_or(FallbackCameraState());
  auto& c = state.camera;

  ImGui::SeparatorText("Объектив");
  static const char* kLensNames[proto::kLensCount] = {"0.5×", "1×", "2×", "3×", "Фронт."};
  for (int i = 0; i < proto::kLensCount; ++i) {
    if (i > 0) ImGui::SameLine();
    const bool available = (range.lenses >> i) & 1;
    ImGui::BeginDisabled(!available);
    if (ImGui::RadioButton(kLensNames[i], c.lens == proto::Lens(i))) {
      c.lens = proto::Lens(i);
      c.zoom = 1.0f;
    }
    ImGui::EndDisabled();
  }
  if (phone && phone->lens != c.lens) ImGui::TextDisabled("Переключение объектива...");
  ImGui::SliderFloat("Зум", &c.zoom, 1.0f, std::max(range.zoomMax, 1.01f), "%.2f×", ImGuiSliderFlags_Logarithmic);
  ImGui::Checkbox("Повернуть на 180°", &c.rotate180);

  ImGui::SeparatorText("Экспозиция");
  const bool manualExposure = ManualToggle("Авто##exposure", c.manualExposure);
  if (manualExposure) {
    c.iso = std::clamp(c.iso, range.isoMin, std::max(range.isoMax, range.isoMin));
    ImGui::SliderFloat("ISO", &c.iso, range.isoMin, range.isoMax, "%.0f", ImGuiSliderFlags_Logarithmic);
    // The slider works on the denominator of 1/N s, which is how shutter speed is read.
    const float slowest = 1e6f / float(std::max(range.shutterMaxUs, 1u));
    const float fastest = 1e6f / float(std::max(range.shutterMinUs, 1u));
    float denominator = std::clamp(1e6f / float(std::max(c.shutterUs, 1u)), slowest, fastest);
    if (ImGui::SliderFloat("Выдержка", &denominator, slowest, fastest, "1/%.0f с", ImGuiSliderFlags_Logarithmic)) {
      c.shutterUs = uint32_t(std::lround(1e6f / denominator));
    }
  } else {
    ImGui::SliderFloat("Экспокоррекция", &c.exposureBias, std::max(range.biasMin, -4.0f),
                       std::min(range.biasMax, 4.0f), "%+.1f EV");
    if (phone) {
      ImGui::TextDisabled("Сейчас: ISO %.0f, 1/%.0f с", phone->iso,
                          1e6f / float(std::max(phone->shutterUs, 1u)));
    }
  }

  ImGui::SeparatorText("Баланс белого");
  if (ManualToggle("Авто##wb", c.manualWhiteBalance)) {
    ImGui::SliderFloat("Температура", &c.temperatureK, 2500.0f, 10000.0f, "%.0f K");
    ImGui::SliderFloat("Оттенок", &c.tint, -150.0f, 150.0f, "%+.0f");
  } else if (phone) {
    ImGui::TextDisabled("Сейчас: %.0f K, оттенок %+.0f", phone->temperatureK, phone->tint);
  }

  ImGui::SeparatorText("Фокус");
  ImGui::BeginDisabled(!range.manualFocusSupported);
  if (ManualToggle("Авто##focus", c.manualFocus)) {
    ImGui::SliderFloat("Дистанция", &c.lensPosition, 0.0f, 1.0f, "%.2f");
    ImGui::TextDisabled("0 = близко, 1 = далеко");
  } else if (phone) {
    ImGui::TextDisabled("Сейчас: %.2f", phone->lensPosition);
  }
  ImGui::EndDisabled();
  if (!range.manualFocusSupported) ImGui::TextDisabled("У этого объектива нет ручного фокуса");

  ImGui::SeparatorText("Фон");
  int blur = c.blur;
  if (ImGui::SliderInt("Размытие", &blur, 0, 100, blur == 0 ? "выкл." : "%d%%")) c.blur = uint8_t(blur);

  ImGui::Spacing();
  if (ImGui::Button("Всё в авто")) {
    proto::CameraSettings reset;
    reset.lens = c.lens;
    reset.rotate180 = c.rotate180;
    c = reset;
  }

  const uint64_t now = SteadyNowMs();
  if (c != connection.DesiredCamera() && now - state.lastCameraSendMs >= kCameraSendIntervalMs) {
    state.lastCameraSendMs = now;
    connection.SetDesiredCamera(c);
  }
}

}  // namespace

void DrawUi(UiState& state, ConnectionManager& connection, Pipeline& pipeline) {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::Begin("Webicam", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

  ImGui::BeginChild("controls", ImVec2(380, 0), ImGuiChildFlags_Borders);
  ImGui::TextWrapped("%s", connection.Detail().c_str());
  if (!state.phoneStatus.empty()) {
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s", state.phoneStatus.c_str());
  }
  if (!state.cameraError.empty()) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", state.cameraError.c_str());
  }

  if (connection.State() == ConnState::ChooseDevice) {
    ImGui::SeparatorText("iPhone");
    for (const auto& device : connection.Devices()) {
      if (ImGui::Selectable(device.serial.c_str())) connection.SelectDevice(device.deviceId);
    }
  }

  ImGui::SeparatorText("Захват на iPhone");
  const auto choices = CaptureChoices(connection.PhoneFormats());
  const auto desired = connection.DesiredConfigure();
  if (state.formatIndex < 0 || state.formatIndex >= int(choices.size())) {
    state.formatIndex = -1;
    for (size_t i = 0; i < choices.size(); ++i) {
      const auto& c = choices[i].configure;
      if (c.width == desired.width && c.height == desired.height && c.fps == desired.fps) state.formatIndex = int(i);
    }
  }
  if (state.bitrateMbps <= 0) state.bitrateMbps = float(desired.bitrateKbps) / 1000.0f;
  const char* preview = state.formatIndex >= 0 ? choices[size_t(state.formatIndex)].label.c_str() : "—";
  ImGui::BeginDisabled(choices.empty());
  if (ImGui::BeginCombo("Разрешение", preview)) {
    for (size_t i = 0; i < choices.size(); ++i) {
      if (ImGui::Selectable(choices[i].label.c_str(), int(i) == state.formatIndex)) {
        state.formatIndex = int(i);
        state.bitrateMbps = float(choices[i].configure.bitrateKbps) / 1000.0f;
      }
    }
    ImGui::EndCombo();
  }
  ImGui::SliderFloat("Битрейт, Мбит/с", &state.bitrateMbps, 2.0f, 80.0f, "%.0f");
  if (ImGui::Button("Применить") && state.formatIndex >= 0) {
    auto configure = choices[size_t(state.formatIndex)].configure;
    configure.bitrateKbps = uint32_t(state.bitrateMbps * 1000.0f);
    connection.SetDesiredConfigure(configure);
    state.chosenConfigure = configure;
  }
  ImGui::EndDisabled();

  DrawCameraControls(state, connection);

  ImGui::SeparatorText("Статистика");
  const auto stats = pipeline.Stats();
  ImGui::Text("Кадров/с: %.1f", stats.fps);
  ImGui::Text("Задержка до камеры: %.1f мс", stats.latencyMs);
  ImGui::Text("Потеряно кадров: %llu", static_cast<unsigned long long>(stats.droppedFrames));
  if (stats.width) ImGui::Text("С телефона: %u×%u", stats.width, stats.height);
  if (stats.output) {
    ImGui::Text("Выход камеры: %u×%u @ %u", stats.output->width, stats.output->height, stats.output->fps);
  } else {
    ImGui::TextDisabled("Камеру Webicam сейчас никто не использует");
  }
  ImGui::EndChild();

  ImGui::SameLine();
  ImGui::BeginChild("preview");
  const ImVec2 area = ImGui::GetContentRegionAvail();
  if (auto* srv = pipeline.Preview(uint32_t(std::max(area.x, 2.0f)), uint32_t(std::max(area.y, 2.0f)))) {
    Microsoft::WRL::ComPtr<ID3D11Resource> resource;
    srv->GetResource(&resource);
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    resource.As(&texture);
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    ImGui::SetCursorPos(ImVec2((area.x - float(desc.Width)) / 2, (area.y - float(desc.Height)) / 2));
    ImGui::Image(ImTextureID(srv), ImVec2(float(desc.Width), float(desc.Height)));
  } else {
    ImGui::SetCursorPos(ImVec2(area.x / 2 - 60, area.y / 2));
    ImGui::TextDisabled("Нет видео");
  }
  ImGui::EndChild();
  ImGui::End();
}

}  // namespace webicam::app
