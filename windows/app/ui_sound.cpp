#include <windows.h>
#include <shellapi.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>

#include "theme.h"
#include "ui.h"
#include "widgets.h"

namespace webicam::app {

namespace {

using theme::Px;

constexpr uint64_t kAudioScanIntervalMs = 3000;

const AudioDevice* FindDevice(const UiState& state, const std::string& id) {
  auto it = std::ranges::find(state.audioDevices, id, &AudioDevice::id);
  return it == state.audioDevices.end() ? nullptr : &*it;
}

// Thin bar filled to the recent peak; the accent while there is sound.
void LevelMeter(float level) {
  const float w = ImGui::GetContentRegionAvail().x;
  const float h = Px(6);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Col(theme::kRaised), h / 2);
  // Speech peaks sit far below full scale; a square root makes quiet talk visible.
  const float t = std::clamp(std::sqrt(level), 0.0f, 1.0f);
  if (t > 0.01f) draw->AddRectFilled(p, ImVec2(p.x + w * t, p.y + h), Col(theme::kAccent), h / 2);
  ImGui::Dummy(ImVec2(w, h));
}

}  // namespace

std::string EffectiveAudioDevice(const UiState& state) {
  return state.audioDevice.empty() ? FindVirtualCable(state.audioDevices) : state.audioDevice;
}

void UpdateAudioOutput(UiState& state, ConnectionManager& connection, UiServices& services) {
  const bool needed = connection.MicEnabled() || state.tab == Tab::Sound;
  const uint64_t now = SteadyNowMs();
  if (needed && (state.audioScanMs == 0 || now - state.audioScanMs >= kAudioScanIntervalMs)) {
    state.audioScanMs = now;
    state.audioDevices = ListAudioOutputs();
  }
  services.audio.SetDevice(connection.MicEnabled() ? EffectiveAudioDevice(state) : std::string());
}

void DrawSoundSection(UiState& state, ConnectionManager& connection, UiServices& services) {
  SectionStart();
  bool enabled = connection.MicEnabled();
  if (SwitchRow("Микрофон iPhone", "##mic", &enabled, theme::GetFonts().medium, theme::kText)) {
    connection.SetMicEnabled(enabled);
  }
  Hint("Звук идёт вместе с видео, по кабелю или Wi-Fi. Задержка около 50 мс");
  if (enabled) {
    ImGui::Dummy(ImVec2(0, Px(8)));
    LevelMeter(services.audio.Level());
    if (connection.CurrentTransport() == Transport::None) Hint("Появится после подключения iPhone");
  }

  Section("Куда выводить");
  ImGui::Dummy(ImVec2(0, Px(2)));
  const std::string cable = FindVirtualCable(state.audioDevices);
  const std::string effective = EffectiveAudioDevice(state);
  const AudioDevice* shown = FindDevice(state, effective);
  std::string preview = shown ? shown->name : "Не выбрано";
  if (state.audioDevice.empty() && shown) preview = "Авто: " + preview;
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::BeginCombo("##audiodevice", preview.c_str())) {
    if (ImGui::IsWindowAppearing()) state.audioDevices = ListAudioOutputs();
    if (ImGui::Selectable("Авто: виртуальный кабель", state.audioDevice.empty(), 0, ImVec2(0, Px(28)))) {
      state.audioDevice.clear();
    }
    ImGui::Separator();
    for (const auto& device : state.audioDevices) {
      ImGui::PushID(device.id.c_str());
      if (ImGui::Selectable(device.name.c_str(), state.audioDevice == device.id, 0, ImVec2(0, Px(28)))) {
        state.audioDevice = device.id;
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }

  ImGui::Dummy(ImVec2(0, Px(6)));
  if (cable.empty()) {
    Callout("Чтобы Discord, Zoom и OBS увидели микрофон iPhone, нужен виртуальный кабель: Windows не даёт "
            "программам создавать микрофоны без драйвера. Установите бесплатный VB-Audio Virtual Cable "
            "и перезагрузите компьютер",
            theme::kAccent);
    ImGui::Dummy(ImVec2(0, Px(4)));
    ImGui::PushFont(nullptr, theme::kSmallSize);
    if (ImGui::TextLink("Скачать VB-Audio Virtual Cable")) {
      ShellExecuteW(services.window, L"open", L"https://vb-audio.com/Cable/", nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::PopFont();
  } else if (effective == cable) {
    Hint("В Discord, Zoom или OBS выберите микрофон «CABLE Output (VB-Audio Virtual Cable)»");
  } else if (!effective.empty()) {
    HintColored("Звук пойдёт в это устройство, а не в микрофон: так его можно только послушать", theme::kWarn);
  }
  const std::string error = services.audio.Error();
  if (enabled && !error.empty()) {
    ImGui::Dummy(ImVec2(0, Px(2)));
    HintColored(error.c_str(), theme::kDanger);
  }
}

}  // namespace webicam::app
