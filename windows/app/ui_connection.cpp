#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numbers>

#include "theme.h"
#include "ui.h"
#include "widgets.h"

namespace webicam::app {

namespace {

using theme::Px;

// ---- Icons: drawn, so they scale with DPI and need no icon font ---------------

enum class Icon { Usb, Wifi, Battery, Thermometer, Mic };

float Stroke() { return std::max(1.0f, Px(1.5f)); }

// USB plug: two prongs above a body, the cable below. `p` is the top left of an s×s box.
void DrawUsb(ImDrawList* draw, ImVec2 p, float s, ImU32 color) {
  const float cx = p.x + s / 2;
  draw->AddRect(ImVec2(cx - s * 0.26f, p.y + s * 0.30f), ImVec2(cx + s * 0.26f, p.y + s * 0.68f), color, Px(1.5f), 0,
                Stroke());
  draw->AddLine(ImVec2(cx - s * 0.11f, p.y + s * 0.08f), ImVec2(cx - s * 0.11f, p.y + s * 0.30f), color, Stroke());
  draw->AddLine(ImVec2(cx + s * 0.11f, p.y + s * 0.08f), ImVec2(cx + s * 0.11f, p.y + s * 0.30f), color, Stroke());
  draw->AddLine(ImVec2(cx, p.y + s * 0.68f), ImVec2(cx, p.y + s * 0.96f), color, Stroke());
}

// Three arcs over a dot.
void DrawWifi(ImDrawList* draw, ImVec2 p, float s, ImU32 color) {
  const ImVec2 c(p.x + s / 2, p.y + s * 0.86f);
  constexpr float pi = std::numbers::pi_v<float>;
  for (float r : {s * 0.30f, s * 0.55f, s * 0.80f}) {
    draw->PathArcTo(c, r, -pi * 0.75f, -pi * 0.25f, 12);
    draw->PathStroke(color, 0, Stroke());
  }
  draw->AddCircleFilled(c, std::max(Px(1.5f), s * 0.08f), color);
}

// Bulb with a stem.
void DrawThermometer(ImDrawList* draw, ImVec2 p, float s, ImU32 color) {
  const float cx = p.x + s / 2;
  const float r = s * 0.17f;
  draw->AddLine(ImVec2(cx, p.y + s * 0.10f), ImVec2(cx, p.y + s * 0.62f), color, std::max(1.0f, Px(3)));
  draw->AddCircleFilled(ImVec2(cx, p.y + s * 0.80f), r, color);
}

// Capsule in a cradle on a stand.
void DrawMic(ImDrawList* draw, ImVec2 p, float s, ImU32 color) {
  const float cx = p.x + s / 2;
  const float w = s * 0.34f;
  draw->AddRectFilled(ImVec2(cx - w / 2, p.y + s * 0.04f), ImVec2(cx + w / 2, p.y + s * 0.58f), color, w / 2);
  constexpr float pi = std::numbers::pi_v<float>;
  draw->PathArcTo(ImVec2(cx, p.y + s * 0.42f), s * 0.32f, 0, pi, 12);
  draw->PathStroke(color, 0, Stroke());
  draw->AddLine(ImVec2(cx, p.y + s * 0.74f), ImVec2(cx, p.y + s * 0.96f), color, Stroke());
}

// Outline with a nub on the right, filled to `level` (0…1); level < 0 = unknown.
void DrawBattery(ImDrawList* draw, ImVec2 p, float s, float level, ImU32 color) {
  const float w = s * 1.45f;
  const float h = s * 0.70f;
  const ImVec2 a(p.x, p.y + (s - h) / 2);
  const ImVec2 b(a.x + w, a.y + h);
  draw->AddRect(a, b, color, Px(2.5f), 0, Stroke());
  draw->AddRectFilled(ImVec2(b.x + Px(1), a.y + h * 0.3f), ImVec2(b.x + Px(3), b.y - h * 0.3f), color, Px(1));
  if (level < 0) return;
  const float inset = Px(2.5f);
  const float fill = (w - inset * 2) * std::clamp(level, 0.0f, 1.0f);
  if (fill > 0.5f) {
    draw->AddRectFilled(ImVec2(a.x + inset, a.y + inset), ImVec2(a.x + inset + fill, b.y - inset), color, Px(1));
  }
}

// Lightning bolt in an s×s box.
void DrawBolt(ImDrawList* draw, ImVec2 p, float s, ImU32 color) {
  const ImVec2 pts[] = {
      {p.x + s * 0.60f, p.y + s * 0.02f}, {p.x + s * 0.18f, p.y + s * 0.58f}, {p.x + s * 0.48f, p.y + s * 0.58f},
      {p.x + s * 0.38f, p.y + s * 0.98f}, {p.x + s * 0.84f, p.y + s * 0.40f}, {p.x + s * 0.54f, p.y + s * 0.40f},
  };
  draw->AddConcavePolyFilled(pts, 6, color);
}

float IconWidth(Icon icon, float s) { return icon == Icon::Battery ? s * 1.45f + Px(3) : s; }

void DrawIcon(ImDrawList* draw, Icon icon, ImVec2 p, float s, ImU32 color, float level = -1) {
  switch (icon) {
    case Icon::Usb: DrawUsb(draw, p, s, color); break;
    case Icon::Wifi: DrawWifi(draw, p, s, color); break;
    case Icon::Battery: DrawBattery(draw, p, s, level, color); break;
    case Icon::Thermometer: DrawThermometer(draw, p, s, color); break;
    case Icon::Mic: DrawMic(draw, p, s, color); break;
  }
}

// ---- Top bar ------------------------------------------------------------------

const char* ThermalLabel(uint8_t thermal) {
  switch (thermal) {
    case 0: return "Норма";
    case 1: return "Тёплый";
    case 2: return "Горячий";
    default: return "Перегрев";
  }
}

ImVec4 ThermalTone(uint8_t thermal) {
  switch (thermal) {
    case 0: return theme::kMuted;
    case 1: return theme::kText;
    case 2: return theme::kWarn;
    default: return theme::kDanger;
  }
}

struct Chip {
  Icon icon;
  std::string text;
  ImVec4 tone;
  const char* tooltip;
  float level = -1;   // battery fill
  bool bolt = false;  // charging
  bool quiet = true;  // tone is the neutral muted one
};

float ChipWidth(const Chip& chip, float iconSize) {
  ImGui::PushFont(theme::GetFonts().medium, theme::kSmallSize);
  const float text = ImGui::CalcTextSize(chip.text.c_str()).x;
  ImGui::PopFont();
  return Px(10) + IconWidth(chip.icon, iconSize) + Px(7) + text + (chip.bolt ? Px(12) : 0) + Px(10);
}

void DrawChip(const Chip& chip, ImVec2 p, float h, float iconSize) {
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const float w = ChipWidth(chip, iconSize);
  draw->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Col(theme::kRaised), h / 2);
  float x = p.x + Px(10);
  DrawIcon(draw, chip.icon, ImVec2(x, p.y + (h - iconSize) / 2), iconSize, Col(chip.tone), chip.level);
  x += IconWidth(chip.icon, iconSize) + Px(7);
  ImGui::PushFont(theme::GetFonts().medium, theme::kSmallSize);
  const ImVec2 ts = ImGui::CalcTextSize(chip.text.c_str());
  // Neutral chips keep the icon quiet and the text readable; warnings colour both.
  draw->AddText(ImVec2(x, p.y + (h - ts.y) / 2), Col(chip.quiet ? theme::kText : chip.tone), chip.text.c_str());
  ImGui::PopFont();
  if (chip.bolt) DrawBolt(draw, ImVec2(x + ts.x + Px(3), p.y + (h - Px(11)) / 2), Px(11), Col(theme::kAccent));
  ImGui::SetCursorScreenPos(p);
  ImGui::PushID(chip.tooltip);
  ImGui::InvisibleButton("##chip", ImVec2(w, h));
  ImGui::PopID();
  ImGui::SetItemTooltip("%s", chip.tooltip);
}

ImVec4 StateTone(ConnState conn) {
  switch (conn) {
    case ConnState::Streaming: return theme::kAccent;
    case ConnState::AppleServiceMissing:
    case ConnState::VersionMismatch: return theme::kDanger;
    case ConnState::WifiNeedsCode:
    case ConnState::ChooseDevice: return theme::kWarn;
    default: return theme::kFaint;
  }
}

// ---- Connection tab -----------------------------------------------------------

int DigitsOnly(ImGuiInputTextCallbackData* data) {
  return data->EventChar >= '0' && data->EventChar <= '9' ? 0 : 1;
}

// A row in the phone list: icon, name, a muted second line, «Подключён» on the right when in use.
bool PhoneRow(const char* id, Icon icon, const std::string& name, const std::string& detail, bool active) {
  const float w = ImGui::GetContentRegionAvail().x;
  const float h = Px(48);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
  const bool hovered = ImGui::IsItemHovered();
  if (hovered && !active) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  if (active || hovered) {
    draw->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Col(active ? theme::kActive : theme::kHover), Px(8));
  }
  const float s = Px(16);
  DrawIcon(draw, icon, ImVec2(p.x + Px(12), p.y + (h - s) / 2), s, Col(active ? theme::kAccent : theme::kMuted));
  const float x = p.x + Px(12) + IconWidth(icon, s) + Px(12);
  const float right = p.x + w - Px(12);
  float badgeW = 0;
  if (active) {
    ImGui::PushFont(nullptr, theme::kSmallSize);
    badgeW = ImGui::CalcTextSize("Подключён").x + Px(8);
    draw->AddText(ImVec2(right - badgeW + Px(8), p.y + (h - ImGui::GetFontSize()) / 2), Col(theme::kAccent),
                  "Подключён");
    ImGui::PopFont();
  }
  draw->PushClipRect(ImVec2(x, p.y), ImVec2(right - badgeW, p.y + h), true);
  ImGui::PushFont(theme::GetFonts().medium, theme::kBodySize - 1);
  draw->AddText(ImVec2(x, p.y + Px(7)), Col(theme::kText), name.c_str());
  ImGui::PopFont();
  ImGui::PushFont(nullptr, theme::kSmallSize - 1);
  draw->AddText(ImVec2(x, p.y + Px(27)), Col(theme::kMuted), detail.c_str());
  ImGui::PopFont();
  draw->PopClipRect();
  return clicked && !active;
}

void DrawCodeEntry(UiState& state, ConnectionManager& connection, const WifiPhone& phone) {
  Section("Код с экрана iPhone");
  Hint(("Webicam на «" + phone.name + "» показывает шесть цифр. Код нужен один раз для этого компьютера").c_str());
  const bool fresh = state.codeInputFor != phone.id;
  if (fresh) {
    state.codeInput[0] = '\0';
    state.codeInputFor = phone.id;
  }
  ImGui::Dummy(ImVec2(0, Px(4)));
  ImGui::PushFont(theme::GetFonts().mono, theme::kTitleSize + 2);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(Px(14), Px(10)));
  ImGui::SetNextItemWidth(-FLT_MIN);
  const bool enter = ImGui::InputTextWithHint("##code", "000000", state.codeInput, 7,
                                              ImGuiInputTextFlags_CallbackCharFilter |
                                                  ImGuiInputTextFlags_EnterReturnsTrue,
                                              DigitsOnly);
  ImGui::PopStyleVar();
  ImGui::PopFont();
  // The manager words a refusal itself; the prompt it shows otherwise starts with «Введите код».
  const std::string detail = connection.Detail();
  if (detail.rfind("Введите код", 0) != 0) {
    ImGui::Dummy(ImVec2(0, Px(2)));
    HintColored(detail.c_str(), theme::kDanger);
  }
  ImGui::Dummy(ImVec2(0, Px(6)));
  const bool complete = std::strlen(state.codeInput) == 6;
  if (PrimaryButton("Подключить", complete) || (enter && complete)) {
    connection.SetWifiCode(phone.id, state.codeInput);
    state.codeInput[0] = '\0';
  }
}

void DrawPhoneList(ConnectionManager& connection, LinkMode mode) {
  const Transport transport = connection.CurrentTransport();
  const auto usb = mode == LinkMode::Wifi ? std::vector<UsbmuxDevice>{} : connection.Devices();
  const auto wifi = mode == LinkMode::Usb ? std::vector<WifiPhone>{} : connection.WifiPhones();
  const auto activeWifi = connection.ActiveWifiId();
  Section("iPhone");
  if (usb.empty() && wifi.empty()) {
    Hint(mode == LinkMode::Usb ? "Подключите iPhone кабелем и откройте на нём Webicam"
                               : "Пока никого не видно. Откройте Webicam на iPhone");
    return;
  }
  ImGui::Dummy(ImVec2(0, Px(2)));
  for (const auto& device : usb) {
    ImGui::PushID(int(device.deviceId));
    const std::string tail = device.serial.size() > 8 ? "…" + device.serial.substr(device.serial.size() - 8)
                                                      : device.serial;
    const bool active = transport == Transport::Usb && usb.size() == 1;
    if (PhoneRow("##usb", Icon::Usb, "iPhone по кабелю", tail, active)) connection.SelectDevice(device.deviceId);
    ImGui::PopID();
  }
  for (const auto& phone : wifi) {
    ImGui::PushID(phone.id.c_str());
    const bool active = activeWifi && *activeWifi == phone.id;
    const std::string where = "Wi-Fi · " + phone.host;
    if (PhoneRow("##wifi", Icon::Wifi, phone.name, where, active)) connection.SelectWifiPhone(phone.id);
    ImGui::PopID();
  }
}

void DrawManualAddress(UiState& state, ConnectionManager& connection) {
  if (!state.addressShown && state.wifiAddress.empty()) {
    ImGui::Dummy(ImVec2(0, Px(8)));
    ImGui::PushFont(nullptr, theme::kSmallSize);
    if (ImGui::TextLink("Не находит iPhone? Указать адрес вручную")) {
      state.addressShown = true;
      std::snprintf(state.addressInput, sizeof(state.addressInput), "%s", state.wifiAddress.c_str());
    }
    ImGui::PopFont();
    return;
  }
  if (!state.addressShown) {
    state.addressShown = true;
    std::snprintf(state.addressInput, sizeof(state.addressInput), "%s", state.wifiAddress.c_str());
  }
  Section("Адрес iPhone");
  Hint("Настройки iPhone → Wi-Fi → (i) у вашей сети → IP-адрес");
  ImGui::Dummy(ImVec2(0, Px(2)));
  ImGui::SetNextItemWidth(-FLT_MIN);
  const bool enter = ImGui::InputTextWithHint("##address", "192.168.1.20", state.addressInput,
                                              sizeof(state.addressInput), ImGuiInputTextFlags_EnterReturnsTrue |
                                                                              ImGuiInputTextFlags_CharsNoBlank);
  ImGui::Dummy(ImVec2(0, Px(2)));
  const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
  ImGui::BeginChild("##apply", ImVec2(half, Px(36)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
  const bool apply = SecondaryButton("Подключить") || enter;
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##clear", ImVec2(half, Px(36)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
  const bool clear = SecondaryButton("Убрать");
  ImGui::EndChild();
  if (apply && state.addressInput[0]) {
    state.wifiAddress = state.addressInput;
    connection.SetWifiAddress(state.wifiAddress);
  }
  if (clear) {
    state.addressInput[0] = '\0';
    state.wifiAddress.clear();
    state.addressShown = false;
    connection.SetWifiAddress("");
  }
}

}  // namespace

void DrawTopBar(UiState& state, ConnectionManager& connection, UiServices& services) {
  const float h = Px(52);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float w = ImGui::GetContentRegionAvail().x;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Col(theme::kSurface));
  draw->AddLine(ImVec2(p.x, p.y + h - Px(0.5f)), ImVec2(p.x + w, p.y + h - Px(0.5f)), Col(theme::kLine),
                std::max(1.0f, Px(1)));
  const float pad = Px(22);

  ImGui::PushFont(theme::GetFonts().semibold, theme::kTitleSize - 2);
  const ImVec2 title = ImGui::CalcTextSize("Webicam");
  draw->AddText(ImVec2(p.x + pad, p.y + (h - title.y) / 2), Col(theme::kText), "Webicam");
  ImGui::PopFont();

  // Chips from the right edge: heat, battery, link. Only what the phone actually reported.
  std::vector<Chip> chips;
  const Transport transport = connection.CurrentTransport();
  if (transport == Transport::Usb || transport == Transport::Direct) {
    chips.push_back({Icon::Usb, "USB", theme::kMuted, "Подключён кабелем"});
  } else if (transport == Transport::Wifi) {
    chips.push_back({Icon::Wifi, "Wi-Fi", theme::kMuted, "Подключён по Wi-Fi. По кабелю задержка меньше"});
  }
  // The tooltip has to outlive the chip list.
  std::string micTooltip;
  if (transport != Transport::None && connection.MicEnabled()) {
    const std::string error = services.audio.Error();
    const std::string device = EffectiveAudioDevice(state);
    Chip mic{Icon::Mic, "Микрофон", theme::kMuted, ""};
    if (device.empty()) {
      micTooltip = "Звук с iPhone некуда вывести: установите VB-Audio Virtual Cable (вкладка «Звук»)";
      mic.tone = theme::kWarn;
      mic.quiet = false;
    } else if (!error.empty()) {
      micTooltip = "Звук с iPhone: " + error;
      mic.tone = theme::kDanger;
      mic.quiet = false;
    } else {
      micTooltip = "Звук с iPhone идёт в выбранное устройство (вкладка «Звук»)";
    }
    mic.tooltip = micTooltip.c_str();
    chips.push_back(mic);
  }
  if (const auto t = connection.PhoneTelemetry()) {
    Chip battery{Icon::Battery, "—", theme::kMuted, "Заряд iPhone"};
    if (t->battery != proto::kBatteryUnknown) {
      battery.text = std::to_string(t->battery) + "%";
      battery.level = float(t->battery) / 100.0f;
      const bool plugged = t->charging == proto::Charging::Charging || t->charging == proto::Charging::Full;
      if (!plugged && t->battery <= 20) {
        battery.tone = t->battery <= 10 ? theme::kDanger : theme::kWarn;
        battery.quiet = false;
      }
    }
    battery.bolt = t->charging == proto::Charging::Charging;
    if (t->charging == proto::Charging::Charging) battery.tooltip = "Заряд iPhone: заряжается";
    if (t->charging == proto::Charging::Full) battery.tooltip = "Заряд iPhone: полностью заряжен";
    chips.push_back(battery);
    Chip heat{Icon::Thermometer, ThermalLabel(t->thermal), ThermalTone(t->thermal),
              "Нагрев iPhone. iOS сообщает уровень, а не градусы; при «Горячем» качество снижается"};
    heat.quiet = t->thermal == 0;
    chips.push_back(heat);
  }
  const float chipH = Px(28);
  const float iconSize = Px(14);
  float x = p.x + w - pad;
  for (auto it = chips.rbegin(); it != chips.rend(); ++it) {
    x -= ChipWidth(*it, iconSize);
    DrawChip(*it, ImVec2(x, p.y + (h - chipH) / 2), chipH, iconSize);
    x -= Px(8);
  }

  // Status between the name and the chips; clicking it opens the Connection tab.
  const ConnState conn = connection.State();
  const float left = p.x + pad + title.x + Px(24);
  const float right = std::max(left, x - Px(16));
  const std::string detail = connection.Detail();
  ImGui::PushFont(nullptr, theme::kSmallSize + 1);
  const float r = Px(3.5f);
  const float cy = p.y + h / 2;
  draw->AddCircleFilled(ImVec2(left + r, cy), r, Col(StateTone(conn)));
  draw->PushClipRect(ImVec2(left, p.y), ImVec2(right, p.y + h), true);
  draw->AddText(ImVec2(left + r * 2 + Px(8), cy - ImGui::GetFontSize() / 2), Col(theme::kMuted), detail.c_str());
  draw->PopClipRect();
  ImGui::PopFont();
  if (right > left) {
    ImGui::SetCursorScreenPos(ImVec2(left, p.y));
    ImGui::InvisibleButton("##status", ImVec2(right - left, h));
    if (ImGui::IsItemHovered()) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      ImGui::SetTooltip("%s", detail.c_str());
    }
    if (ImGui::IsItemClicked()) {
      state.tab = Tab::Connection;
      state.connectionTabAuto = false;
    }
  }
  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
}

void DrawConnectionSection(UiState& state, ConnectionManager& connection) {
  Section("Способ подключения");
  ImGui::Dummy(ImVec2(0, Px(2)));
  static const char* kModes[kLinkModeCount] = {"Авто", "USB", "Wi-Fi"};
  const LinkMode mode = connection.GetLinkMode();
  const int picked = Segmented("link", kModes, kLinkModeCount, int(mode), 0x7);
  if (picked >= 0 && LinkMode(picked) != mode) connection.SetLinkMode(LinkMode(picked));
  switch (connection.GetLinkMode()) {
    case LinkMode::Auto: Hint("Кабель, если iPhone подключён к компьютеру, иначе Wi-Fi"); break;
    case LinkMode::Usb: Hint("Только кабель: минимальная задержка"); break;
    case LinkMode::Wifi: Hint("Компьютер и iPhone должны быть в одной сети"); break;
  }

  const ConnState conn = connection.State();
  if (conn != ConnState::Streaming && conn != ConnState::WifiNeedsCode) {
    ImGui::Dummy(ImVec2(0, Px(8)));
    const ImVec4 tone = conn == ConnState::AppleServiceMissing || conn == ConnState::VersionMismatch ? theme::kDanger
                                                                                                    : theme::kAccent;
    Callout(connection.Detail(), tone);
  }

  if (const auto request = connection.CodeRequest()) DrawCodeEntry(state, connection, *request);
  DrawPhoneList(connection, connection.GetLinkMode());
  if (connection.GetLinkMode() != LinkMode::Usb) DrawManualAddress(state, connection);
}

}  // namespace webicam::app
