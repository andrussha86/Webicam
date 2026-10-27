#include <imgui.h>

#include <algorithm>
#include <cstdio>

#include "theme.h"
#include "ui.h"
#include "widgets.h"

namespace webicam::app {

namespace {

using theme::Px;

const char* NameErrorText(PresetNameError error) {
  switch (error) {
    case PresetNameError::Empty: return "Введите название";
    case PresetNameError::TooLong: return "Не длиннее 32 символов";
    case PresetNameError::BadChar: return "Без символов [ ] и переносов строки";
    case PresetNameError::Builtin: return "Так называется встроенный пресет";
    case PresetNameError::Exists: return "Такой пресет уже есть. Нажмите ещё раз, чтобы заменить";
    case PresetNameError::None: break;
  }
  return "";
}

const Preset* FindPreset(const UiState& state, const std::string& name) {
  for (const auto& p : BuiltinPresets()) {
    if (p.name == name) return &p;
  }
  for (const auto& p : state.userPresets) {
    if (p.name == name) return &p;
  }
  return nullptr;
}

void SetHint(UiState& state, std::string text, bool error) {
  state.presetHint = std::move(text);
  state.presetHintIsError = error;
}

void Apply(const Preset& preset, UiState& state, ConnectionManager& connection, UiServices& services) {
  SetHint(state, "", false);
  const auto phoneFormats = connection.PhoneFormats();
  std::optional<proto::Configure> configure = preset.configure;
  if (!phoneFormats.empty()) configure = MatchFormat(preset.configure, phoneFormats);
  if (configure) {
    connection.SetDesiredConfigure(*configure);
    state.chosenConfigure = *configure;
  } else {
    char text[128];
    std::snprintf(text, sizeof(text), "Формат %u×%u, %u к/с недоступен на этом iPhone", preset.configure.width,
                  preset.configure.height, unsigned(preset.configure.fps));
    SetHint(state, text, false);
  }

  state.camera = preset.camera;
  const bool isFile = !preset.backgroundImage.starts_with("builtin:");
  if (isFile) {
    const auto files = services.library.List();
    if (std::find(files.begin(), files.end(), preset.backgroundImage) == files.end()) {
      if (state.camera.backgroundMode == proto::BackgroundMode::Image) {
        state.camera.backgroundMode = proto::BackgroundMode::Off;
        SetHint(state, "Картинка не найдена", false);
      }
    }
  }
  state.backgroundImage = preset.backgroundImage;
  services.setBackground(state.backgroundImage);
  connection.SetDesiredCamera(state.camera);
  state.activePreset = preset.name;
}

bool Persist(UiState& state) {
  if (SavePresets(DefaultPresetsPath(), state.userPresets)) return true;
  SetHint(state, "Не удалось сохранить пресет", true);
  return false;
}

// Centred modal in the sidebar's colours: surface background (fields and buttons stay visible on it),
// the same radius as the preview, the title as text inside instead of ImGui's blue title bar.
bool BeginDialog(const char* id, const char* title) {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(Px(360), 0), ImGuiCond_Appearing);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Px(20), Px(18)));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Px(10));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, std::max(1.0f, Px(1)));
  ImGui::PushStyleColor(ImGuiCol_PopupBg, theme::kSurface);
  ImGui::PushStyleColor(ImGuiCol_Border, theme::kLine);
  ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(theme::kBase.x, theme::kBase.y, theme::kBase.z, 0.6f));
  const bool open = ImGui::BeginPopupModal(
      id, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar(3);
  if (open) {
    Text(title, theme::GetFonts().semibold, theme::kBodySize + 1, theme::kText);
    ImGui::Dummy(ImVec2(0, Px(4)));
  }
  return open;
}

void DrawSaveDialog(UiState& state, ConnectionManager& connection) {
  static char name[160] = "";
  static std::string error;
  static std::string confirmedOverwrite;  // name the user already pressed «Сохранить» for once
  if (!BeginDialog("Новый пресет", "Новый пресет")) return;
  if (ImGui::IsWindowAppearing()) {
    std::snprintf(name, sizeof(name), "%s", state.activePreset.c_str());
    if (FindPreset(state, state.activePreset) && FindPreset(state, state.activePreset)->builtin) name[0] = '\0';
    error.clear();
    confirmedOverwrite.clear();
    ImGui::SetKeyboardFocusHere();
  }
  Text("Название", theme::GetFonts().regular, theme::kSmallSize, theme::kMuted);
  ImGui::SetNextItemWidth(-FLT_MIN);
  const bool enter = ImGui::InputText("##name", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue);
  if (ImGui::IsItemEdited()) {
    error.clear();
    confirmedOverwrite.clear();
  }
  if (!error.empty()) HintColored(error.c_str(), theme::kDanger);
  ImGui::Dummy(ImVec2(0, Px(6)));

  const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
  ImGui::BeginChild("##save", ImVec2(half, Px(36)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
  const bool save = PrimaryButton("Сохранить", true) || enter;
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##cancel", ImVec2(half, Px(36)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
  const bool cancel = SecondaryButton("Отмена");
  ImGui::EndChild();

  if (save) {
    const std::string wanted = name;
    const bool overwrite = !confirmedOverwrite.empty() && SameNameIgnoreCase(confirmedOverwrite, wanted);
    const auto check = ValidatePresetName(wanted, state.userPresets, overwrite);
    if (check == PresetNameError::Exists) confirmedOverwrite = wanted;
    if (check != PresetNameError::None) {
      error = NameErrorText(check);
    } else {
      Preset preset{wanted, false, connection.DesiredConfigure(), state.camera, state.backgroundImage};
      auto& user = state.userPresets;
      std::erase_if(user, [&](const Preset& p) { return SameNameIgnoreCase(p.name, wanted); });
      user.push_back(std::move(preset));
      if (Persist(state)) SetHint(state, "", false);
      state.activePreset = wanted;
      ImGui::CloseCurrentPopup();
    }
  }
  if (cancel || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

void DrawDeleteDialog(UiState& state) {
  if (!BeginDialog("Удалить пресет", "Удалить пресет")) return;
  const std::string question = "Удалить пресет „" + state.activePreset + "“?";
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(question.c_str());
  ImGui::PopTextWrapPos();
  ImGui::Dummy(ImVec2(0, Px(6)));
  const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
  ImGui::BeginChild("##delete", ImVec2(half, Px(36)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
  const bool remove = PrimaryButton("Удалить", true);
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##keep", ImVec2(half, Px(36)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
  const bool cancel = SecondaryButton("Отмена");
  ImGui::EndChild();
  if (remove) {
    std::erase_if(state.userPresets, [&](const Preset& p) { return p.name == state.activePreset; });
    Persist(state);
    state.activePreset.clear();
    ImGui::CloseCurrentPopup();
  }
  if (cancel || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

}  // namespace

void DrawPresetSection(UiState& state, ConnectionManager& connection, UiServices& services) {
  const Preset* active = FindPreset(state, state.activePreset);

  // Title with «изменён» on the right when the current settings drifted from the preset.
  SectionStart();
  const float y = ImGui::GetCursorPosY();
  const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
  Text("Пресет", theme::GetFonts().medium, theme::kBodySize, theme::kText);
  if (active && !PresetMatches(*active, connection.DesiredConfigure(), state.camera, state.backgroundImage)) {
    ImGui::PushFont(nullptr, theme::kSmallSize);
    const float w = ImGui::CalcTextSize("изменён").x;
    const float afterTitle = ImGui::GetCursorPosY();
    ImGui::SetCursorPos(ImVec2(right - w, y + Px(2)));
    ImGui::PushStyleColor(ImGuiCol_Text, theme::kMuted);
    ImGui::TextUnformatted("изменён");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SetCursorPosY(afterTitle);
  }

  ImGui::SetNextItemWidth(-FLT_MIN);
  const char* preview = active ? active->name.c_str() : "Не выбран";
  if (ImGui::BeginCombo("##preset", preview)) {
    const Preset* chosen = nullptr;
    for (const auto& p : BuiltinPresets()) {
      if (ImGui::Selectable(p.name.c_str(), &p == active, 0, ImVec2(0, Px(28)))) chosen = &p;
    }
    if (!state.userPresets.empty()) ImGui::Separator();
    for (const auto& p : state.userPresets) {
      ImGui::PushID(&p);
      if (ImGui::Selectable(p.name.c_str(), &p == active, 0, ImVec2(0, Px(28)))) chosen = &p;
      ImGui::PopID();
    }
    ImGui::EndCombo();
    if (chosen) {
      const Preset copy = *chosen;  // Apply may not touch the list, but keep it independent anyway
      Apply(copy, state, connection, services);
      active = FindPreset(state, state.activePreset);
    }
  }

  ImGui::Dummy(ImVec2(0, Px(2)));
  const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
  ImGui::BeginChild("##saveas", ImVec2(half, Px(36)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
  if (SecondaryButton("Сохранить как")) ImGui::OpenPopup("Новый пресет");
  DrawSaveDialog(state, connection);
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##remove", ImVec2(half, Px(36)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
  ImGui::BeginDisabled(!active || active->builtin);
  if (SecondaryButton("Удалить")) ImGui::OpenPopup("Удалить пресет");
  ImGui::EndDisabled();
  DrawDeleteDialog(state);
  ImGui::EndChild();

  if (!state.presetHint.empty()) HintColored(state.presetHint.c_str(), state.presetHintIsError ? theme::kDanger : theme::kMuted);
}

}  // namespace webicam::app
