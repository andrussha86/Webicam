#include "ui.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

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

}  // namespace

void DrawUi(UiState& state, ConnectionManager& connection, Pipeline& pipeline) {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::Begin("Webicam", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

  ImGui::BeginChild("controls", ImVec2(340, 0), ImGuiChildFlags_Borders);
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
  }
  ImGui::EndDisabled();

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
