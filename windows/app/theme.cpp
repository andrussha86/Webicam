#include "theme.h"

#include <windows.h>

#include "resource.h"

namespace webicam::app::theme {

namespace {

Fonts g_fonts;
float g_scale = 1.0f;

ImFont* AddResourceFont(int id) {
  HRSRC info = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
  HGLOBAL handle = info ? LoadResource(nullptr, info) : nullptr;
  void* data = handle ? LockResource(handle) : nullptr;
  if (!data) return nullptr;
  // Resource memory lives as long as the exe; the atlas must not free it.
  ImFontConfig config;
  config.FontDataOwnedByAtlas = false;
  return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(data, int(SizeofResource(nullptr, info)), kBodySize, &config);
}

}  // namespace

void LoadFonts() {
  g_fonts.regular = AddResourceFont(IDR_FONT_REGULAR);  // first one added is the default
  if (!g_fonts.regular) g_fonts.regular = ImGui::GetIO().Fonts->AddFontDefault();
  g_fonts.medium = AddResourceFont(IDR_FONT_MEDIUM);
  g_fonts.semibold = AddResourceFont(IDR_FONT_SEMIBOLD);
  g_fonts.mono = AddResourceFont(IDR_FONT_MONO);
  if (!g_fonts.medium) g_fonts.medium = g_fonts.regular;
  if (!g_fonts.semibold) g_fonts.semibold = g_fonts.medium;
  if (!g_fonts.mono) g_fonts.mono = g_fonts.regular;
}

const Fonts& GetFonts() { return g_fonts; }

float Scale() { return g_scale; }

bool ReducedMotion() {
  BOOL animate = TRUE;
  SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0);
  return !animate;
}

void Apply(float scale) {
  g_scale = scale > 0 ? scale : 1.0f;
  ImGuiStyle style;
  style.FontSizeBase = kBodySize;
  style.WindowPadding = ImVec2(0, 0);
  style.WindowRounding = 0;
  style.WindowBorderSize = 0;
  style.ChildRounding = 0;
  style.ChildBorderSize = 0;
  style.PopupRounding = 8;
  style.PopupBorderSize = 1;
  style.FramePadding = ImVec2(10, 8);
  style.FrameRounding = 6;
  style.FrameBorderSize = 0;
  style.ItemSpacing = ImVec2(10, 10);
  style.ItemInnerSpacing = ImVec2(8, 6);
  style.ScrollbarSize = 8;
  style.ScrollbarRounding = 8;
  style.GrabMinSize = 14;
  style.GrabRounding = 8;
  style.SelectableTextAlign = ImVec2(0, 0.5f);
  style.DisabledAlpha = 0.4f;
  style.ScaleAllSizes(g_scale);
  style.FontScaleDpi = g_scale;

  ImVec4* c = style.Colors;
  c[ImGuiCol_Text] = kText;
  c[ImGuiCol_TextDisabled] = kMuted;
  c[ImGuiCol_WindowBg] = kBase;
  c[ImGuiCol_ChildBg] = Rgb(0, 0);
  c[ImGuiCol_PopupBg] = kRaised;
  c[ImGuiCol_Border] = kLine;
  c[ImGuiCol_BorderShadow] = Rgb(0, 0);
  c[ImGuiCol_FrameBg] = kRaised;
  c[ImGuiCol_FrameBgHovered] = kHover;
  c[ImGuiCol_FrameBgActive] = kActive;
  c[ImGuiCol_ScrollbarBg] = Rgb(0, 0);
  c[ImGuiCol_ScrollbarGrab] = kActive;
  c[ImGuiCol_ScrollbarGrabHovered] = kFaint;
  c[ImGuiCol_ScrollbarGrabActive] = kMuted;
  c[ImGuiCol_CheckMark] = kAccent;
  c[ImGuiCol_SliderGrab] = kAccent;
  c[ImGuiCol_SliderGrabActive] = kAccentHi;
  c[ImGuiCol_Button] = kRaised;
  c[ImGuiCol_ButtonHovered] = kHover;
  c[ImGuiCol_ButtonActive] = kActive;
  c[ImGuiCol_Header] = kActive;
  c[ImGuiCol_HeaderHovered] = kHover;
  c[ImGuiCol_HeaderActive] = kActive;
  c[ImGuiCol_Separator] = kLine;
  c[ImGuiCol_SeparatorHovered] = kLine;
  c[ImGuiCol_SeparatorActive] = kLine;
  c[ImGuiCol_TextSelectedBg] = Rgb(0xA9BACB, 0.3f);
  c[ImGuiCol_NavCursor] = kAccent;
  ImGui::GetStyle() = style;
}

}  // namespace webicam::app::theme
