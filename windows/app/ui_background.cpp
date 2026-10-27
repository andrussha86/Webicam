#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "file_dialog.h"
#include "theme.h"
#include "ui.h"
#include "widgets.h"

namespace webicam::app {

namespace {

using theme::Px;

constexpr uint32_t kSwatches[] = {0x1F2125, 0xE6E7E9, 0xA9BACB, 0x1F3A2E, 0x1C2A44, 0xC9B79C};

std::string BuiltinRef(int index) { return "builtin:" + std::to_string(index); }

void SelectPicture(UiState& state, UiServices& services, std::string ref) {
  state.backgroundImage = std::move(ref);
  services.setBackground(state.backgroundImage);
}

// One 16:9 tile; returns true when clicked. `selected` draws the accent outline.
bool Tile(const char* id, ID3D11ShaderResourceView* image, ImVec2 size, bool selected, ImVec2* outMin) {
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::SetNextItemAllowOverlap();
  const bool clicked = ImGui::InvisibleButton(id, size);
  const bool hovered = ImGui::IsItemHovered();
  if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const float rounding = ImGui::GetStyle().FrameRounding;
  const ImVec2 q(p.x + size.x, p.y + size.y);
  if (image) {
    draw->AddImageRounded(ImTextureID(image), p, q, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, rounding);
  } else {
    draw->AddRectFilled(p, q, Col(theme::kRaised), rounding);
  }
  if (hovered) draw->AddRectFilled(p, q, Col(theme::kHover, 0.35f), rounding);
  if (selected) draw->AddRect(p, q, Col(theme::kAccent), rounding, 0, Px(2));
  *outMin = p;
  return clicked;
}

void DrawPictures(UiState& state, ConnectionManager& connection, UiServices& services) {
  const float gap = Px(6);
  const float w = std::floor((ImGui::GetContentRegionAvail().x - gap * 3) / 4);
  const ImVec2 size(w, std::round(w * 9 / 16));
  const auto files = services.library.List();
  const int count = kBuiltinBackgroundCount + int(files.size()) + 1;
  std::optional<std::string> remove;
  bool add = false;

  for (int i = 0; i < count; ++i) {
    if (i % 4 != 0) ImGui::SameLine(0, gap);
    ImGui::PushID(i);
    ImVec2 p;
    if (i < kBuiltinBackgroundCount) {
      const auto ref = BuiltinRef(i);
      if (Tile("##tile", services.thumbnails.Get(ref, services.library), size, state.backgroundImage == ref, &p)) {
        SelectPicture(state, services, ref);
      }
    } else if (i < count - 1) {
      const auto& name = files[size_t(i - kBuiltinBackgroundCount)];
      if (Tile("##tile", services.thumbnails.Get(name, services.library), size, state.backgroundImage == name, &p)) {
        SelectPicture(state, services, name);
      }
      // «Убрать» in the corner while the tile is hovered, on a dark plate so it reads over any picture.
      if (ImGui::IsMouseHoveringRect(p, ImVec2(p.x + size.x, p.y + size.y))) {
        const float s = Px(18);
        const ImVec2 x(p.x + size.x - s - Px(3), p.y + Px(3));
        const ImVec2 after = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(x);
        if (ImGui::InvisibleButton("##remove", ImVec2(s, s))) remove = name;
        const bool hot = ImGui::IsItemHovered();
        if (hot) {
          ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
          ImGui::SetTooltip("Убрать");
        }
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(x, ImVec2(x.x + s, x.y + s), Col(theme::kBase, hot ? 0.95f : 0.8f),
                            ImGui::GetStyle().FrameRounding / 2);
        const float m = Px(5.5f);
        draw->AddLine(ImVec2(x.x + m, x.y + m), ImVec2(x.x + s - m, x.y + s - m), Col(theme::kText), Px(1.5f));
        draw->AddLine(ImVec2(x.x + s - m, x.y + m), ImVec2(x.x + m, x.y + s - m), Col(theme::kText), Px(1.5f));
        ImGui::SetCursorScreenPos(after);
      }
    } else {
      if (Tile("##add", nullptr, size, false, &p)) add = true;
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Добавить картинку");
      ImGui::PushFont(theme::GetFonts().regular, theme::kTitleSize);
      const ImVec2 ts = ImGui::CalcTextSize("+");
      ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + (size.x - ts.x) / 2, p.y + (size.y - ts.y) / 2),
                                          Col(theme::kMuted), "+");
      ImGui::PopFont();
    }
    ImGui::PopID();
  }

  const bool builtinSelected = state.backgroundImage.starts_with("builtin:");
  if (builtinSelected) {
    const int index = std::atoi(state.backgroundImage.c_str() + 8);
    Hint(BuiltinBackgroundName(index));
  } else {
    Hint("Своя картинка");
  }

  if (add) {
    if (auto path = PickImageFile(services.window)) {
      auto added = services.library.Add(*path);
      if (added) {
        state.backgroundError.clear();
        SelectPicture(state, services, *added);
      } else {
        state.backgroundError = added.error();
      }
    }
  }
  if (remove) {
    std::vector<Preset> all = state.userPresets;
    auto removed = services.library.Remove(*remove, all);
    if (removed) {
      state.backgroundError.clear();
      services.thumbnails.Forget(*remove);
      if (state.backgroundImage == *remove) SelectPicture(state, services, BuiltinRef(0));
    } else {
      state.backgroundError = removed.error();
    }
  }
  if (!state.backgroundError.empty()) HintColored(state.backgroundError.c_str(), theme::kDanger);
  (void)connection;
}

ImVec4 ToColor(uint32_t rgb) { return theme::Rgb(rgb); }

uint32_t FromColor(const float c[3]) {
  const auto channel = [](float v) { return uint32_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
  return channel(c[0]) << 16 | channel(c[1]) << 8 | channel(c[2]);
}

void DrawColors(UiState& state) {
  const float s = Px(28);
  const float gap = Px(8);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const float rounding = ImGui::GetStyle().FrameRounding;
  for (int i = 0; i < int(std::size(kSwatches)); ++i) {
    if (i) ImGui::SameLine(0, gap);
    ImGui::PushID(i);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##swatch", ImVec2(s, s))) state.camera.backgroundColor = kSwatches[i];
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    draw->AddRectFilled(p, ImVec2(p.x + s, p.y + s), Col(ToColor(kSwatches[i])), rounding);
    draw->AddRect(p, ImVec2(p.x + s, p.y + s), Col(theme::kLine), rounding, 0, std::max(1.0f, Px(1)));
    if (state.camera.backgroundColor == kSwatches[i]) {
      draw->AddRect(ImVec2(p.x - Px(3), p.y - Px(3)), ImVec2(p.x + s + Px(3), p.y + s + Px(3)), Col(theme::kAccent),
                    rounding + Px(2), 0, Px(2));
    }
    ImGui::PopID();
  }
  ImGui::SameLine(0, gap);
  const ImVec4 current = ToColor(state.camera.backgroundColor);
  float rgb[3] = {current.x, current.y, current.z};
  const bool custom = std::find(std::begin(kSwatches), std::end(kSwatches), state.camera.backgroundColor) ==
                      std::end(kSwatches);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2((s - ImGui::GetFrameHeight()) / 2 + ImGui::GetStyle().FramePadding.x,
                                                         (s - ImGui::GetFontSize()) / 2));
  if (ImGui::ColorEdit3("##custom", rgb, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
    state.camera.backgroundColor = FromColor(rgb);
  }
  ImGui::PopStyleVar();
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Свой цвет");
  if (custom) {
    const ImVec2 q = ImGui::GetItemRectMax();
    draw->AddRect(ImVec2(p.x - Px(3), p.y - Px(3)), ImVec2(q.x + Px(3), q.y + Px(3)), Col(theme::kAccent),
                  rounding + Px(2), 0, Px(2));
  }
}

void DrawReactions(const proto::CameraState& phone) {
  ImGui::Dummy(ImVec2(0, Px(6)));
  Text("Реакции", theme::GetFonts().regular, theme::kSmallSize, theme::kText);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Px(4));
  if (!phone.reactionsSupported) {
    Hint("Недоступны в этом формате");
  } else if (phone.reactionsEnabled) {
    HintColored("Включены: покажите руками большой палец, „викторию“ или сердечко", theme::kAccent);
  } else {
    Hint("Выключены: на iPhone откройте Пункт управления, Видеоэффекты, Реакции");
  }
}

}  // namespace

void DrawBackgroundSection(UiState& state, ConnectionManager& connection, UiServices& services) {
  auto& c = state.camera;
  Section("Фон");
  static const char* kModes[proto::kBackgroundModeCount] = {"Выкл.", "Размытие", "Картинка", "Цвет"};
  const int mode = Segmented("bgmode", kModes, proto::kBackgroundModeCount, int(c.backgroundMode), 0xF);
  if (mode >= 0) {
    c.backgroundMode = proto::BackgroundMode(mode);
    if (c.backgroundMode == proto::BackgroundMode::Blur && c.blur == 0) c.blur = 60;
  }
  ImGui::Dummy(ImVec2(0, Px(4)));

  switch (c.backgroundMode) {
    case proto::BackgroundMode::Blur: {
      char shown[16];
      std::snprintf(shown, sizeof(shown), "%u%%", unsigned(c.blur));
      float blur = c.blur;
      if (Slider("Сила", &blur, 1.0f, 100.0f, shown)) c.blur = uint8_t(std::lround(blur));
      break;
    }
    case proto::BackgroundMode::Image:
      DrawPictures(state, connection, services);
      break;
    case proto::BackgroundMode::Color:
      DrawColors(state);
      break;
    case proto::BackgroundMode::Off:
      break;
  }

  const auto phone = connection.PhoneCamera();
  if (phone && phone->reactionsEnabled && c.backgroundMode != proto::BackgroundMode::Off) {
    Hint("Реакции видны только поверх вас");
  }
  if (phone) DrawReactions(*phone);
}

}  // namespace webicam::app
