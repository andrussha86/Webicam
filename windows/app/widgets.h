#pragma once

#include <imgui.h>

#include <cstdint>
#include <string>

namespace webicam::app {

// Sidebar building blocks shared by every section. Colours come from theme.h only.

// Goes through ImGui so BeginDisabled() fades custom drawing too.
ImU32 Col(const ImVec4& color, float alpha = 1.0f);
ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t);
void Text(const char* text, ImFont* font, float size, const ImVec4& color);
// Small muted wrapped text under a control.
void Hint(const char* text);
// Same as Hint in another colour: the accent for "on", kDanger for errors.
void HintColored(const char* text, const ImVec4& color);
void SectionStart();
bool Switch(const char* id, bool* value);
bool SwitchRow(const char* label, const char* id, bool* value, ImFont* font, const ImVec4& color,
               const char* switchLabel = nullptr);
// Section title with an "Авто" switch; returns true while the group is manual.
bool AutoSection(const char* title, const char* id, bool& manual);
void Section(const char* title);
void LabelValue(const char* label, const char* value);
bool Slider(const char* label, float* value, float min, float max, const char* shown, bool logarithmic = false);
// Row of equal segments; returns the clicked index or -1.
int Segmented(const char* id, const char* const* labels, int count, int selected, uint32_t enabledMask);
bool PrimaryButton(const char* label, bool enabled);
bool SecondaryButton(const char* label);
void Callout(const std::string& text, const ImVec4& tone);

}  // namespace webicam::app
