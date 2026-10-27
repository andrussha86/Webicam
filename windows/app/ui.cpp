#include "ui.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "theme.h"
#include "widgets.h"
#include "webicam/vcam_logic.h"

namespace webicam::app {

namespace {

using theme::Px;

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
      std::snprintf(label, sizeof(label), "%u×%u, %u к/с", f.width, f.height, fps);
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


// ---- Sidebar ---------------------------------------------------------------

void DrawHeader(const UiState& state, ConnectionManager& connection) {
  Text("Webicam", theme::GetFonts().semibold, theme::kTitleSize, theme::kText);
  ImGui::Dummy(ImVec2(0, Px(2)));

  // The dot is the connection state: steel when streaming, red when the user must fix something.
  const ConnState conn = connection.State();
  ImVec4 dot = theme::kFaint;
  if (conn == ConnState::Streaming) dot = theme::kAccent;
  if (conn == ConnState::AppleServiceMissing || conn == ConnState::VersionMismatch) dot = theme::kDanger;
  ImGui::PushFont(nullptr, theme::kSmallSize + 1);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float r = Px(3.5f);
  ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + r, p.y + ImGui::GetFontSize() / 2 + Px(1)), r, Col(dot));
  ImGui::SetCursorScreenPos(ImVec2(p.x + r * 2 + Px(8), p.y));
  ImGui::PushStyleColor(ImGuiCol_Text, theme::kMuted);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(connection.Detail().c_str());
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
  ImGui::PopFont();

  if (!state.cameraError.empty()) {
    ImGui::Dummy(ImVec2(0, Px(4)));
    Callout(state.cameraError, theme::kDanger);
  }
  if (!state.phoneStatus.empty()) {
    ImGui::Dummy(ImVec2(0, Px(4)));
    Callout(state.phoneStatus, theme::kAccent);
  }

  if (conn == ConnState::ChooseDevice) {
    Section("Выберите iPhone");
    for (const auto& device : connection.Devices()) {
      ImGui::PushID(int(device.deviceId));
      if (ImGui::Selectable(device.serial.c_str(), false, 0, ImVec2(0, Px(32)))) {
        connection.SelectDevice(device.deviceId);
      }
      if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      ImGui::PopID();
    }
  }
}

void DrawVideoSection(UiState& state, ConnectionManager& connection) {
  Section("Видео");
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

  ImGui::BeginDisabled(choices.empty());
  Text("Разрешение и частота", theme::GetFonts().regular, theme::kSmallSize, theme::kMuted);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Px(4));
  const char* preview = choices.empty()             ? "Появится после подключения"
                        : state.formatIndex >= 0 ? choices[size_t(state.formatIndex)].label.c_str()
                                                 : "Выберите формат";
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::BeginCombo("##format", preview)) {
    for (size_t i = 0; i < choices.size(); ++i) {
      const bool selected = int(i) == state.formatIndex;
      if (ImGui::Selectable(choices[i].label.c_str(), selected, 0, ImVec2(0, Px(28)))) {
        state.formatIndex = int(i);
        state.bitrateMbps = float(choices[i].configure.bitrateKbps) / 1000.0f;
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  char shown[32];
  std::snprintf(shown, sizeof(shown), "%.0f Мбит/с", state.bitrateMbps);
  if (Slider("Битрейт", &state.bitrateMbps, 2.0f, 80.0f, shown)) state.bitrateMbps = std::round(state.bitrateMbps);

  // Enabled only when the choice differs from what the phone is told to send.
  bool dirty = false;
  proto::Configure configure{};
  if (state.formatIndex >= 0) {
    configure = choices[size_t(state.formatIndex)].configure;
    configure.bitrateKbps = uint32_t(state.bitrateMbps * 1000.0f);
    dirty = configure.width != desired.width || configure.height != desired.height || configure.fps != desired.fps ||
            configure.bitrateKbps != desired.bitrateKbps;
  }
  ImGui::Dummy(ImVec2(0, Px(2)));
  if (PrimaryButton(dirty || choices.empty() ? "Применить" : "Применено", dirty)) {
    connection.SetDesiredConfigure(configure);
    state.chosenConfigure = configure;
  }
  ImGui::EndDisabled();
}

void DrawCameraControls(UiState& state, ConnectionManager& connection, UiServices& services) {
  const auto phone = connection.PhoneCamera();
  const proto::CameraState range = phone.value_or(FallbackCameraState());
  auto& c = state.camera;
  char shown[64];

  Section("Объектив");
  static const char* kLensNames[proto::kLensCount] = {"0.5×", "1×", "2×", "3×", "Фронт"};
  const int lens = Segmented("lens", kLensNames, proto::kLensCount, int(c.lens), range.lenses);
  if (lens >= 0 && proto::Lens(lens) != c.lens) {
    c.lens = proto::Lens(lens);
    c.zoom = 1.0f;
  }
  if (phone && phone->lens != c.lens) Hint("Переключение объектива…");
  std::snprintf(shown, sizeof(shown), "%.2f×", c.zoom);
  Slider("Зум", &c.zoom, 1.0f, std::max(range.zoomMax, 1.01f), shown, true);
  SwitchRow("Повернуть на 180°", "##rotate", &c.rotate180, theme::GetFonts().regular, theme::kText);

  if (AutoSection("Экспозиция", "##exposure", c.manualExposure)) {
    c.iso = std::clamp(c.iso, range.isoMin, std::max(range.isoMax, range.isoMin));
    std::snprintf(shown, sizeof(shown), "%.0f", c.iso);
    Slider("ISO", &c.iso, range.isoMin, range.isoMax, shown, true);
    // The slider works on the denominator of 1/N s, which is how shutter speed is read.
    const float slowest = 1e6f / float(std::max(range.shutterMaxUs, 1u));
    const float fastest = 1e6f / float(std::max(range.shutterMinUs, 1u));
    float denominator = std::clamp(1e6f / float(std::max(c.shutterUs, 1u)), slowest, fastest);
    std::snprintf(shown, sizeof(shown), "1/%.0f с", denominator);
    if (Slider("Выдержка", &denominator, slowest, fastest, shown, true)) {
      c.shutterUs = uint32_t(std::lround(1e6f / denominator));
    }
  } else {
    std::snprintf(shown, sizeof(shown), "%+.1f EV", c.exposureBias);
    Slider("Экспокоррекция", &c.exposureBias, std::max(range.biasMin, -4.0f), std::min(range.biasMax, 4.0f), shown);
    if (phone) {
      std::snprintf(shown, sizeof(shown), "Сейчас ISO %.0f, 1/%.0f с", phone->iso,
                    1e6f / float(std::max(phone->shutterUs, 1u)));
      Hint(shown);
    }
  }

  if (AutoSection("Баланс белого", "##wb", c.manualWhiteBalance)) {
    std::snprintf(shown, sizeof(shown), "%.0f K", c.temperatureK);
    Slider("Температура", &c.temperatureK, 2500.0f, 10000.0f, shown);
    std::snprintf(shown, sizeof(shown), "%+.0f", c.tint);
    Slider("Оттенок", &c.tint, -150.0f, 150.0f, shown);
  } else if (phone) {
    std::snprintf(shown, sizeof(shown), "Сейчас %.0f K, оттенок %+.0f", phone->temperatureK, phone->tint);
    Hint(shown);
  }

  ImGui::BeginDisabled(!range.manualFocusSupported);
  if (AutoSection("Фокус", "##focus", c.manualFocus)) {
    std::snprintf(shown, sizeof(shown), "%.2f", c.lensPosition);
    Slider("Дистанция", &c.lensPosition, 0.0f, 1.0f, shown);
    Hint("0 ближе всего, 1 дальше всего");
  } else if (phone) {
    std::snprintf(shown, sizeof(shown), "Сейчас %.2f", phone->lensPosition);
    Hint(shown);
  }
  ImGui::EndDisabled();
  if (!range.manualFocusSupported) Hint("У этого объектива нет ручного фокуса");
  ImGui::Dummy(ImVec2(0, Px(2)));
  ImGui::BeginDisabled(c.manualFocus);
  SwitchRow("Следить за лицом", "##face", &c.faceFocus, theme::GetFonts().regular, theme::kText);
  ImGui::EndDisabled();
  if (c.manualFocus) Hint("Работает в авто-режиме");

  DrawBackgroundSection(state, connection, services);

  ImGui::Dummy(ImVec2(0, Px(14)));
  if (SecondaryButton("Сбросить всё в авто")) {
    // Only exposure, white balance, focus and zoom go back to auto; lens, rotation and background stay.
    proto::CameraSettings reset;
    reset.lens = c.lens;
    reset.rotate180 = c.rotate180;
    reset.faceFocus = c.faceFocus;
    reset.backgroundMode = c.backgroundMode;
    reset.backgroundColor = c.backgroundColor;
    reset.blur = c.blur;
    c = reset;
  }

  const uint64_t now = SteadyNowMs();
  if (c != connection.DesiredCamera() && now - state.lastCameraSendMs >= kCameraSendIntervalMs) {
    state.lastCameraSendMs = now;
    connection.SetDesiredCamera(c);
  }
}

// ---- Preview ---------------------------------------------------------------

void Metric(const char* label, const char* value, bool dim = false) {
  ImGui::BeginGroup();
  Text(label, theme::GetFonts().regular, 12.0f, theme::kMuted);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Px(6));
  Text(value, theme::GetFonts().mono, theme::kBodySize - 1, dim ? theme::kMuted : theme::kText);
  ImGui::EndGroup();
  ImGui::SameLine(0, Px(36));
}

void DrawStats(Pipeline& pipeline) {
  const auto stats = pipeline.Stats();
  char value[64];
  std::snprintf(value, sizeof(value), "%.1f", stats.fps);
  Metric("Кадров/с", value);
  std::snprintf(value, sizeof(value), "%.1f мс", stats.latencyMs);
  Metric("Задержка", value);
  std::snprintf(value, sizeof(value), "%llu", static_cast<unsigned long long>(stats.droppedFrames));
  Metric("Потеряно", value);
  if (stats.width) {
    std::snprintf(value, sizeof(value), "%u×%u", stats.width, stats.height);
    Metric("С телефона", value);
  }
  if (stats.output) {
    std::snprintf(value, sizeof(value), "%u×%u, %u к/с", stats.output->width, stats.output->height,
                  stats.output->fps);
    Metric("Камера Webicam", value);
  } else {
    Metric("Камера Webicam", "никто не использует", true);
  }
  ImGui::NewLine();
}

void DrawPreview(ConnectionManager& connection, Pipeline& pipeline) {
  const float pad = Px(20);
  const float statsH = Px(56);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const ImVec2 area(std::max(avail.x - pad * 2, 2.0f), std::max(avail.y - pad - statsH, 2.0f));
  const ImVec2 a(origin.x + pad, origin.y + pad);
  ImDrawList* draw = ImGui::GetWindowDrawList();

  if (auto* srv = pipeline.Preview(uint32_t(area.x), uint32_t(area.y))) {
    Microsoft::WRL::ComPtr<ID3D11Resource> resource;
    srv->GetResource(&resource);
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    resource.As(&texture);
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    const ImVec2 p(a.x + (area.x - float(desc.Width)) / 2, a.y + (area.y - float(desc.Height)) / 2);
    draw->AddImageRounded(ImTextureID(srv), p, ImVec2(p.x + float(desc.Width), p.y + float(desc.Height)),
                          ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, Px(10));
  } else {
    // Empty state: a quiet frame where the picture will be.
    draw->AddRectFilled(a, ImVec2(a.x + area.x, a.y + area.y), Col(theme::kSurface), Px(10));
    const bool streaming = connection.State() == ConnState::Streaming;
    const char* title = streaming ? "Ждём первый кадр" : "Нет видео";
    const char* hint = streaming ? "Изображение появится через пару секунд"
                                 : "Подключите iPhone кабелем и откройте на нём Webicam";
    ImGui::PushFont(theme::GetFonts().medium, theme::kBodySize + 2);
    const ImVec2 ts = ImGui::CalcTextSize(title);
    const float cy = a.y + area.y / 2;
    draw->AddText(ImVec2(a.x + (area.x - ts.x) / 2, cy - ts.y - Px(4)), Col(theme::kText), title);
    ImGui::PopFont();
    ImGui::PushFont(nullptr, theme::kSmallSize + 1);
    const ImVec2 hs = ImGui::CalcTextSize(hint);
    draw->AddText(ImVec2(a.x + (area.x - hs.x) / 2, cy + Px(4)), Col(theme::kMuted), hint);
    ImGui::PopFont();
  }

  ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + area.y + Px(12)));
  DrawStats(pipeline);
}

}  // namespace

void DrawUi(UiState& state, ConnectionManager& connection, Pipeline& pipeline, UiServices& services) {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::Begin("Webicam", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoScrollbar);

  const float sidebarW = Px(360);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::kSurface);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Px(22), Px(20)));
  ImGui::BeginChild("controls", ImVec2(sidebarW, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  DrawHeader(state, connection);
  DrawPresetSection(state, connection, services);
  DrawVideoSection(state, connection);
  DrawCameraControls(state, connection, services);
  ImGui::Dummy(ImVec2(0, Px(4)));
  ImGui::EndChild();
  const ImVec2 edge = ImGui::GetItemRectMax();
  ImGui::GetWindowDrawList()->AddLine(ImVec2(edge.x, viewport->WorkPos.y), edge, Col(theme::kLine),
                                      std::max(1.0f, Px(1)));

  ImGui::SameLine(0, 0);
  ImGui::BeginChild("preview", ImVec2(0, 0), 0, ImGuiWindowFlags_NoScrollbar);
  DrawPreview(connection, pipeline);
  ImGui::EndChild();
  ImGui::End();
}

}  // namespace webicam::app
