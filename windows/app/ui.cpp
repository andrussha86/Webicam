#include "ui.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "theme.h"
#include "widgets.h"
#include "webicam/frame_formats.h"
#include "webicam/vcam_logic.h"

namespace webicam::app {

namespace {

using theme::Px;

constexpr uint64_t kCameraSendIntervalMs = 50;  // at most 20 CameraSettings per second
constexpr float kFallbackZoomMax = 5.0f;

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
  s.zoomMax = kFallbackZoomMax;
  s.manualFocusSupported = true;
  s.iso = 100;
  s.shutterUs = 10000;
  s.temperatureK = 5000;
  s.lensPosition = 0.5f;
  return s;
}

void Label(const char* text) {
  Text(text, theme::GetFonts().regular, theme::kSmallSize, theme::kMuted);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Px(4));
}

// ---- Sidebar ---------------------------------------------------------------

// Messages that need attention, above everything else in the sidebar.
void DrawCallouts(const UiState& state) {
  if (!state.cameraError.empty()) {
    Callout(state.cameraError, theme::kDanger);
    ImGui::Dummy(ImVec2(0, Px(8)));
  }
  if (!state.phoneStatus.empty()) {
    Callout(state.phoneStatus, theme::kAccent);
    ImGui::Dummy(ImVec2(0, Px(8)));
  }
}

void DrawTabs(UiState& state) {
  ImGui::Dummy(ImVec2(0, Px(12)));
  static const char* kTabs[kTabCount] = {"Связь", "Кадр", "Камера", "Фон", "Звук"};
  const int tab = Segmented("tabs", kTabs, kTabCount, int(state.tab), 0x1F);
  if (tab >= 0) {
    state.tab = Tab(tab);
    state.connectionTabAuto = false;
  }
}

// The Connection tab opens when the user has to do something there, and closes again once the
// stream starts if it was the app that opened it.
void FollowConnection(UiState& state, ConnState conn) {
  if (state.lastConnState == conn) return;
  state.lastConnState = conn;
  const bool needsUser = conn == ConnState::WifiNeedsCode || conn == ConnState::ChooseDevice;
  if (needsUser && state.tab != Tab::Connection) {
    state.tab = Tab::Connection;
    state.connectionTabAuto = true;
  }
  if (conn == ConnState::Streaming && state.tab == Tab::Connection && state.connectionTabAuto) state.tab = Tab::Frame;
}

// Shape, size, frame rate and bitrate; every change goes to the phone at once.
void DrawFormat(UiState& state, ConnectionManager& connection, Pipeline& pipeline) {
  Section("Формат");
  const auto phone = connection.PhoneFormats();
  const auto desired = connection.DesiredConfigure();
  const Aspect aspect = AspectOf(desired.width, desired.height);
  std::optional<proto::Configure> next;

  ImGui::BeginDisabled(phone.empty());
  const char* aspects[kAspectCount];
  for (int i = 0; i < kAspectCount; ++i) aspects[i] = AspectLabel(Aspect(i));
  const uint32_t available = phone.empty() ? 0xF : AvailableAspects(phone);
  const int picked = Segmented("aspect", aspects, kAspectCount, int(aspect), available);
  if (picked >= 0 && Aspect(picked) != aspect) next = ConfigureForAspect(desired, Aspect(picked), phone);
  ImGui::Dummy(ImVec2(0, Px(2)));

  const auto sizes = OutputSizes(phone, aspect);
  const auto current = std::find_if(sizes.begin(), sizes.end(), [&](const proto::CaptureFormat& f) {
    return f.width == desired.width && f.height == desired.height;
  });
  char shown[64];
  std::snprintf(shown, sizeof(shown), "%u×%u", desired.width, desired.height);
  Label("Разрешение");
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::BeginCombo("##size", shown)) {
    for (const auto& f : sizes) {
      char label[32];
      std::snprintf(label, sizeof(label), "%u×%u", f.width, f.height);
      const bool selected = current != sizes.end() && f == *current;
      if (ImGui::Selectable(label, selected, 0, ImVec2(0, Px(28))) && !selected) {
        const uint8_t fps = std::min(desired.fps, f.maxFps);
        next = proto::Configure{f.width, f.height, fps, proto::DefaultBitrateKbps(f.width, f.height, fps)};
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  ImGui::Dummy(ImVec2(0, Px(2)));
  static const char* kRates[] = {"30 к/с", "60 к/с"};
  const uint8_t maxFps = current != sizes.end() ? current->maxFps : 60;
  const int rate = Segmented("fps", kRates, 2, desired.fps > 30 ? 1 : 0, maxFps >= 60 ? 0x3 : 0x1);
  if (rate >= 0) {
    const uint8_t fps = rate ? 60 : 30;
    if (fps != desired.fps) {
      next = proto::Configure{desired.width, desired.height, fps,
                              proto::DefaultBitrateKbps(desired.width, desired.height, fps)};
    }
  }

  // The slider is local while dragged and goes to the phone on release, so the encoder is not
  // rebuilt for every pixel of movement.
  if (!state.bitrateDragging) state.bitrateMbps = float(desired.bitrateKbps) / 1000.0f;
  std::snprintf(shown, sizeof(shown), "%.0f Мбит/с", state.bitrateMbps);
  if (Slider("Битрейт", &state.bitrateMbps, 2.0f, 80.0f, shown)) state.bitrateMbps = std::round(state.bitrateMbps);
  state.bitrateDragging = ImGui::IsItemActive();
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    proto::Configure c = desired;
    c.bitrateKbps = uint32_t(state.bitrateMbps * 1000.0f);
    if (c != desired) next = c;
  }
  ImGui::EndDisabled();
  if (phone.empty()) Hint("Варианты появятся после подключения iPhone");
  else if (!(available >> int(Aspect::Classic) & 1)) Hint("4:3 этот iPhone не снимает");

  // Portrait into a landscape camera (or back) is fitted with black bars; the app has to pick the size.
  const auto stats = pipeline.Stats();
  if (stats.output && (stats.output->width >= stats.output->height) != (desired.width >= desired.height)) {
    std::snprintf(shown, sizeof(shown), "%u×%u", desired.width, desired.height);
    const std::string text = "Приложение взяло камеру Webicam как " + std::to_string(stats.output->width) + "×" +
                             std::to_string(stats.output->height) + ", поэтому по краям полосы. Выберите в нём " +
                             shown + ".";
    HintColored(text.c_str(), theme::kAccent);
  }

  if (next) {
    connection.SetDesiredConfigure(*next);
    state.chosenConfigure = *next;
  }
}

void DrawLens(UiState& state, const proto::CameraState& range, const std::optional<proto::CameraState>& phone) {
  auto& c = state.camera;
  char shown[64];
  Section("Объектив");
  static const char* kLensNames[proto::kLensCount] = {"0.5×", "1×", "2×", "3×", "Фронт"};
  const int lens = Segmented("lens", kLensNames, proto::kLensCount, int(c.lens), range.lenses);
  if (lens >= 0 && proto::Lens(lens) != c.lens) {
    c.lens = proto::Lens(lens);
    c.zoom = 1.0f;
    c.panX = c.panY = 0.5f;
  }
  if (phone && phone->lens != c.lens) Hint("Переключение объектива…");
  std::snprintf(shown, sizeof(shown), "%.2f×", c.zoom);
  if (Slider("Зум", &c.zoom, 1.0f, std::max(range.zoomMax, 1.01f), shown, true)) ClampPan(c);
  if (c.zoom > 1.005f) {
    Hint("Перетащите картинку в превью, чтобы выбрать область кадра; колесо мыши меняет зум");
    if (std::abs(c.panX - 0.5f) > 0.001f || std::abs(c.panY - 0.5f) > 0.001f) {
      ImGui::Dummy(ImVec2(0, Px(2)));
      if (SecondaryButton("Вернуть в центр")) c.panX = c.panY = 0.5f;
    }
  }
}

void DrawOrientation(UiState& state, const std::optional<proto::CameraState>& phone) {
  auto& c = state.camera;
  Section("Ориентация");
  static const char* kRotations[proto::kRotationCount] = {"Авто", "Как есть", "180°"};
  const int rotation = Segmented("rotation", kRotations, proto::kRotationCount, int(c.rotation), 0x7);
  if (rotation >= 0) c.rotation = proto::Rotation(rotation);
  if (c.rotation == proto::Rotation::Auto) {
    Hint(phone && phone->flipped ? "Телефон перевёрнут, картинка повёрнута на 180°"
                                 : "Картинка перевернётся сама, если перевернуть телефон");
  }
  ImGui::Dummy(ImVec2(0, Px(4)));
  SwitchRow("Отразить по горизонтали", "##mirrorh", &c.mirrorHorizontal, theme::GetFonts().regular, theme::kText);
  ImGui::Dummy(ImVec2(0, Px(2)));
  SwitchRow("Отразить по вертикали", "##mirrorv", &c.mirrorVertical, theme::GetFonts().regular, theme::kText);
}

void DrawExposure(UiState& state, const proto::CameraState& range, const std::optional<proto::CameraState>& phone) {
  auto& c = state.camera;
  char shown[64];

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

  ImGui::Dummy(ImVec2(0, Px(14)));
  if (SecondaryButton("Всё в авто")) {
    // Only this tab: exposure, white balance and focus.
    const proto::CameraSettings d;
    c.manualExposure = d.manualExposure;
    c.iso = d.iso;
    c.shutterUs = d.shutterUs;
    c.exposureBias = d.exposureBias;
    c.manualWhiteBalance = d.manualWhiteBalance;
    c.temperatureK = d.temperatureK;
    c.tint = d.tint;
    c.manualFocus = d.manualFocus;
    c.lensPosition = d.lensPosition;
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

// Small plate with text over the picture.
void Badge(ImDrawList* draw, ImVec2 at, const char* text) {
  ImGui::PushFont(nullptr, theme::kSmallSize);
  const ImVec2 ts = ImGui::CalcTextSize(text);
  const ImVec2 pad(Px(8), Px(5));
  draw->AddRectFilled(at, ImVec2(at.x + ts.x + pad.x * 2, at.y + ts.y + pad.y * 2), Col(theme::kBase, 0.75f), Px(6));
  draw->AddText(ImVec2(at.x + pad.x, at.y + pad.y), Col(theme::kText), text);
  ImGui::PopFont();
}

// Camo-style framing on the picture itself: drag moves the zoomed area, the wheel zooms around the
// cursor, a double click centres it. The preview shows the phone's output, so all of this works in
// output coordinates, which is also how the phone reads panX/panY.
void FrameInteraction(UiState& state, float zoomMax, ImVec2 p, ImVec2 size) {
  auto& c = state.camera;
  ImGui::SetCursorScreenPos(p);
  ImGui::InvisibleButton("##frame", size);
  const bool hovered = ImGui::IsItemHovered();
  const bool zoomed = c.zoom > 1.005f;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImGuiIO& io = ImGui::GetIO();

  if (hovered && io.MouseWheel != 0) {
    const float u = (io.MousePos.x - p.x) / size.x - 0.5f;
    const float v = (io.MousePos.y - p.y) / size.y - 0.5f;
    // The point of the full frame under the cursor stays under the cursor.
    const float fx = c.panX + u / c.zoom;
    const float fy = c.panY + v / c.zoom;
    c.zoom = std::clamp(c.zoom * std::pow(1.15f, io.MouseWheel), 1.0f, zoomMax);
    c.panX = fx - u / c.zoom;
    c.panY = fy - v / c.zoom;
    ClampPan(c);
  }
  if (zoomed && ImGui::IsItemActive() && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
    c.panX -= io.MouseDelta.x / size.x / c.zoom;
    c.panY -= io.MouseDelta.y / size.y / c.zoom;
    ClampPan(c);
  }
  if (zoomed && hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) c.panX = c.panY = 0.5f;
  if (zoomed && (hovered || ImGui::IsItemActive())) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

  if (!zoomed) {
    if (hovered) Badge(draw, ImVec2(p.x + Px(12), p.y + Px(12)), "Колесо мыши: зум");
    return;
  }
  // Where the zoomed area sits in the whole frame.
  const float mapW = Px(132);
  const float mapH = std::round(mapW * size.y / size.x);
  const ImVec2 m(p.x + size.x - mapW - Px(12), p.y + size.y - mapH - Px(12));
  draw->AddRectFilled(m, ImVec2(m.x + mapW, m.y + mapH), Col(theme::kBase, 0.7f), Px(4));
  draw->AddRect(m, ImVec2(m.x + mapW, m.y + mapH), Col(theme::kText, 0.35f), Px(4), 0, std::max(1.0f, Px(1)));
  const float w = mapW / c.zoom, h = mapH / c.zoom;
  const ImVec2 a(m.x + (c.panX * mapW - w / 2), m.y + (c.panY * mapH - h / 2));
  draw->AddRectFilled(a, ImVec2(a.x + w, a.y + h), Col(theme::kAccent, 0.25f), Px(2));
  draw->AddRect(a, ImVec2(a.x + w, a.y + h), Col(theme::kAccent), Px(2), 0, Px(1.5f));
  if (hovered) {
    Badge(draw, ImVec2(p.x + Px(12), p.y + Px(12)), "Тяните, чтобы выбрать область. Двойной щелчок: по центру");
  }
}

void DrawPreview(UiState& state, ConnectionManager& connection, Pipeline& pipeline) {
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
    const ImVec2 size(float(desc.Width), float(desc.Height));
    const ImVec2 p(a.x + (area.x - size.x) / 2, a.y + (area.y - size.y) / 2);
    draw->AddImageRounded(ImTextureID(srv), p, ImVec2(p.x + size.x, p.y + size.y), ImVec2(0, 0), ImVec2(1, 1),
                          IM_COL32_WHITE, Px(10));
    const auto phone = connection.PhoneCamera();
    FrameInteraction(state, phone ? std::max(phone->zoomMax, 1.0f) : kFallbackZoomMax, p, size);
  } else {
    // Empty state: a quiet frame where the picture will be.
    draw->AddRectFilled(a, ImVec2(a.x + area.x, a.y + area.y), Col(theme::kSurface), Px(10));
    const bool streaming = connection.State() == ConnState::Streaming;
    const char* title = streaming ? "Ждём первый кадр" : "Нет видео";
    const char* hint = streaming ? "Изображение появится через пару секунд"
                                 : "Подключите iPhone кабелем или по Wi-Fi и откройте на нём Webicam";
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

void ClampPan(proto::CameraSettings& c) {
  const float half = 0.5f / std::max(c.zoom, 1.0f);
  c.panX = std::clamp(c.panX, half, 1.0f - half);
  c.panY = std::clamp(c.panY, half, 1.0f - half);
}

void DrawUi(UiState& state, ConnectionManager& connection, Pipeline& pipeline, UiServices& services) {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::Begin("Webicam", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoScrollbar);

  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0));
  UpdateAudioOutput(state, connection, services);
  DrawTopBar(state, connection, services);
  ImGui::PopStyleVar();
  FollowConnection(state, connection.State());

  const float sidebarW = Px(360);
  const float bodyTop = ImGui::GetCursorScreenPos().y;
  ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::kSurface);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Px(22), Px(20)));
  ImGui::BeginChild("controls", ImVec2(sidebarW, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  PollImagePicker(state, services);
  DrawCallouts(state);
  DrawPresetSection(state, connection, services);
  DrawTabs(state);

  const auto phone = connection.PhoneCamera();
  const proto::CameraState range = phone.value_or(FallbackCameraState());
  switch (state.tab) {
    case Tab::Connection:
      DrawConnectionSection(state, connection);
      break;
    case Tab::Frame:
      DrawFormat(state, connection, pipeline);
      DrawLens(state, range, phone);
      DrawOrientation(state, phone);
      break;
    case Tab::Camera:
      DrawExposure(state, range, phone);
      break;
    case Tab::Background:
      DrawBackgroundSection(state, connection, services);
      break;
    case Tab::Sound:
      DrawSoundSection(state, connection, services);
      break;
  }
  ImGui::Dummy(ImVec2(0, Px(4)));
  ImGui::EndChild();
  const ImVec2 edge = ImGui::GetItemRectMax();
  ImGui::GetWindowDrawList()->AddLine(ImVec2(edge.x, bodyTop), edge, Col(theme::kLine),
                                      std::max(1.0f, Px(1)));

  ImGui::SameLine(0, 0);
  ImGui::BeginChild("preview", ImVec2(0, 0), 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  DrawPreview(state, connection, pipeline);
  ImGui::EndChild();
  ImGui::End();

  // Sidebar and preview both edit the camera; one throttled send for whatever changed this frame.
  const uint64_t now = SteadyNowMs();
  if (state.camera != connection.DesiredCamera() && now - state.lastCameraSendMs >= kCameraSendIntervalMs) {
    state.lastCameraSendMs = now;
    connection.SetDesiredCamera(state.camera);
  }
}

}  // namespace webicam::app
