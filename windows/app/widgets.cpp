#include "widgets.h"

#include <algorithm>
#include <cmath>

#include "theme.h"

namespace webicam::app {

using theme::Px;

// ---- Drawing helpers -------------------------------------------------------

// Goes through ImGui so BeginDisabled() fades custom drawing too.
ImU32 Col(const ImVec4& color, float alpha) {
  return ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, color.w * alpha));
}

ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t) {
  return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

void Text(const char* text, ImFont* font, float size, const ImVec4& color) {
  ImGui::PushFont(font, size);
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
  ImGui::PopFont();
}

void Hint(const char* text) {
  ImGui::PushFont(nullptr, theme::kSmallSize);
  ImGui::PushStyleColor(ImGuiCol_Text, theme::kMuted);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(text);
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
  ImGui::PopFont();
}

// Line between sidebar sections, then the section title.
void SectionStart() {
  ImGui::Dummy(ImVec2(0, Px(10)));
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y), Col(theme::kLine),
                                      std::max(1.0f, Px(1)));
  ImGui::Dummy(ImVec2(0, Px(8)));
}

// iOS-style switch; animates unless Windows animations are off.
bool Switch(const char* id, bool* value) {
  const ImVec2 size(Px(34), Px(20));
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const bool clicked = ImGui::InvisibleButton(id, size);
  if (clicked) *value = !*value;
  const bool hovered = ImGui::IsItemHovered();
  if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

  ImGuiStorage* storage = ImGui::GetStateStorage();
  const ImGuiID key = ImGui::GetItemID();
  const float target = *value ? 1.0f : 0.0f;
  float t = storage->GetFloat(key, target);
  const float step = theme::ReducedMotion() ? 1.0f : ImGui::GetIO().DeltaTime / 0.14f;
  t = target > t ? std::min(target, t + step) : std::max(target, t - step);
  storage->SetFloat(key, t);
  const float eased = t * t * (3 - 2 * t);

  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec4 off = hovered ? theme::kHover : theme::kActive;
  const ImVec4 on = hovered ? theme::kAccentHi : theme::kAccent;
  draw->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), Col(Mix(off, on, eased)), size.y / 2);
  const float radius = size.y / 2 - Px(3);
  const ImVec2 knob(p.x + size.y / 2 + eased * (size.x - size.y), p.y + size.y / 2);
  draw->AddCircleFilled(knob, radius, Col(Mix(theme::kMuted, theme::kOnAccent, eased)));
  return clicked;
}

// Text on the left, a switch on the right edge of the row.
bool SwitchRow(const char* label, const char* id, bool* value, ImFont* font, const ImVec4& color,
               const char* switchLabel) {
  const float rowH = Px(20);
  const float y = ImGui::GetCursorPosY();
  const float left = ImGui::GetCursorPosX();
  const float right = left + ImGui::GetContentRegionAvail().x;
  ImGui::PushFont(font, 0.0f);
  ImGui::SetCursorPosY(y + (rowH - ImGui::GetFontSize()) / 2);
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  ImGui::TextUnformatted(label);
  ImGui::PopStyleColor();
  ImGui::PopFont();
  if (switchLabel) {
    ImGui::PushFont(nullptr, theme::kSmallSize);
    const float w = ImGui::CalcTextSize(switchLabel).x;
    ImGui::SetCursorPos(ImVec2(right - Px(34) - Px(10) - w, y + (rowH - ImGui::GetFontSize()) / 2));
    ImGui::PushStyleColor(ImGuiCol_Text, theme::kMuted);
    ImGui::TextUnformatted(switchLabel);
    ImGui::PopStyleColor();
    ImGui::PopFont();
  }
  ImGui::SetCursorPos(ImVec2(right - Px(34), y));
  return Switch(id, value);
}

// Section title with an "Авто" switch; returns true while the group is manual.
bool AutoSection(const char* title, const char* id, bool& manual) {
  SectionStart();
  bool automatic = !manual;
  if (SwitchRow(title, id, &automatic, theme::GetFonts().medium, theme::kText, "Авто")) manual = !automatic;
  return manual;
}

void Section(const char* title) {
  SectionStart();
  Text(title, theme::GetFonts().medium, theme::kBodySize, theme::kText);
}

// Muted label on the left, value in mono on the right.
void LabelValue(const char* label, const char* value) {
  const float left = ImGui::GetCursorPosX();
  const float right = left + ImGui::GetContentRegionAvail().x;
  const float y = ImGui::GetCursorPosY();
  Text(label, theme::GetFonts().regular, theme::kSmallSize, theme::kMuted);
  ImGui::PushFont(theme::GetFonts().mono, theme::kSmallSize);
  ImGui::SetCursorPos(ImVec2(right - ImGui::CalcTextSize(value).x, y));
  ImGui::TextUnformatted(value);
  ImGui::PopFont();
}

// Label and value on one line, a slim full-width slider under them.
bool Slider(const char* label, float* value, float min, float max, const char* shown, bool logarithmic) {
  LabelValue(label, shown);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Px(4));

  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->ChannelsSplit(2);
  draw->ChannelsSetCurrent(1);
  ImGui::PushID(label);
  // A small font makes a low frame; ImGui insets the grab by 2 px on each side.
  ImGui::PushFont(nullptr, 12.0f);
  const float frameH = Px(18);
  const float grab = frameH - 4;
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, (frameH - ImGui::GetFontSize()) / 2));
  ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, grab);
  ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, grab / 2);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
  ImGui::SetNextItemWidth(-FLT_MIN);
  const bool changed = ImGui::SliderFloat("##slider", value, min, max, "",
                                          logarithmic ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None);
  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar(3);
  ImGui::PopFont();
  ImGui::PopID();
  const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
  if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

  // Track and filled part, under the grab.
  draw->ChannelsSetCurrent(0);
  const ImVec2 a = ImGui::GetItemRectMin();
  const ImVec2 b = ImGui::GetItemRectMax();
  float t = 0;
  if (max > min) {
    t = logarithmic && min > 0 ? std::log(*value / min) / std::log(max / min) : (*value - min) / (max - min);
  }
  t = std::clamp(t, 0.0f, 1.0f);
  const float x0 = a.x + 2 + grab / 2;
  const float x1 = b.x - 2 - grab / 2;
  const float cy = (a.y + b.y) / 2;
  const float h = Px(2);
  draw->AddRectFilled(ImVec2(x0, cy - h), ImVec2(x1, cy + h), Col(hot ? theme::kHover : theme::kActive), h);
  draw->AddRectFilled(ImVec2(x0, cy - h), ImVec2(x0 + t * (x1 - x0), cy + h), Col(theme::kAccent, 0.55f), h);
  draw->ChannelsMerge();
  return changed;
}

// Row of equal segments; returns the clicked index or -1.
int Segmented(const char* id, const char* const* labels, int count, int selected, uint32_t enabledMask) {
  const float w = ImGui::GetContentRegionAvail().x;
  const float h = Px(32);
  const float inset = Px(3);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Col(theme::kRaised), Px(8));
  const float segW = (w - inset * 2) / float(count);
  int clicked = -1;
  ImGui::PushID(id);
  for (int i = 0; i < count; ++i) {
    const ImVec2 sp(p.x + inset + segW * float(i), p.y + inset);
    const ImVec2 size(segW, h - inset * 2);
    ImGui::SetCursorScreenPos(sp);
    ImGui::BeginDisabled(!((enabledMask >> i) & 1));
    if (ImGui::InvisibleButton(labels[i], size)) clicked = i;
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (i == selected) {
      draw->AddRectFilled(sp, ImVec2(sp.x + size.x, sp.y + size.y), Col(theme::kActive), Px(6));
    } else if (hovered) {
      draw->AddRectFilled(sp, ImVec2(sp.x + size.x, sp.y + size.y), Col(theme::kHover), Px(6));
    }
    ImFont* font = i == selected ? theme::GetFonts().medium : theme::GetFonts().regular;
    ImGui::PushFont(font, theme::kSmallSize + 1);
    const ImVec2 ts = ImGui::CalcTextSize(labels[i]);
    draw->AddText(ImVec2(sp.x + (size.x - ts.x) / 2, sp.y + (size.y - ts.y) / 2),
                  Col(i == selected ? theme::kText : theme::kMuted), labels[i]);
    ImGui::PopFont();
    ImGui::EndDisabled();
  }
  ImGui::PopID();
  ImGui::SetCursorScreenPos(p);
  ImGui::Dummy(ImVec2(w, h));
  return clicked;
}

bool PrimaryButton(const char* label, bool enabled) {
  ImGui::PushStyleColor(ImGuiCol_Button, theme::kAccent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::kAccentHi);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::kAccentHi);
  ImGui::PushStyleColor(ImGuiCol_Text, theme::kOnAccent);
  ImGui::PushFont(theme::GetFonts().medium, 0.0f);
  ImGui::BeginDisabled(!enabled);
  const bool clicked = ImGui::Button(label, ImVec2(-FLT_MIN, Px(36)));
  if (ImGui::IsItemHovered() && enabled) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  ImGui::EndDisabled();
  ImGui::PopFont();
  ImGui::PopStyleColor(4);
  return clicked;
}

bool SecondaryButton(const char* label) {
  ImGui::PushFont(theme::GetFonts().medium, 0.0f);
  const bool clicked = ImGui::Button(label, ImVec2(-FLT_MIN, Px(36)));
  if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  ImGui::PopFont();
  return clicked;
}

// Tinted box for messages that need attention.
void Callout(const std::string& text, const ImVec4& tone) {
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float w = ImGui::GetContentRegionAvail().x;
  const ImVec2 pad(Px(12), Px(10));
  ImGui::PushFont(nullptr, theme::kSmallSize + 1);
  const ImVec2 ts = ImGui::CalcTextSize(text.c_str(), nullptr, false, w - pad.x * 2);
  const ImVec2 q(p.x + w, p.y + ts.y + pad.y * 2);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(p, q, Col(tone, 0.10f), Px(8));
  draw->AddRect(p, q, Col(tone, 0.35f), Px(8), 0, std::max(1.0f, Px(1)));
  ImGui::SetCursorScreenPos(ImVec2(p.x + pad.x, p.y + pad.y));
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - pad.x * 2);
  ImGui::TextUnformatted(text.c_str());
  ImGui::PopTextWrapPos();
  ImGui::PopFont();
  ImGui::SetCursorScreenPos(p);
  ImGui::Dummy(ImVec2(w, q.y - p.y));
}

void HintColored(const char* text, const ImVec4& color) {
  ImGui::PushFont(nullptr, theme::kSmallSize);
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(text);
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
  ImGui::PopFont();
}

}  // namespace webicam::app
