// webicam.exe: connects to the iPhone, decodes, publishes to the "Webicam"
// virtual camera and shows a preview with controls.
//   webicam.exe [--direct host:port]
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <memory>
#include <string>

#include "ui.h"
#include "webicam/connection_manager.h"
#include "webicam/media_foundation.h"
#include "webicam/pipeline.h"
#include "webicam/single_instance.h"
#include "webicam/virtual_cam_host.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

using Microsoft::WRL::ComPtr;
using namespace webicam;

namespace {

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kTrayOpen = 1;
constexpr UINT kTrayExit = 2;

struct App {
  HWND hwnd = nullptr;
  D3DContext d3d;
  ComPtr<IDXGISwapChain1> swapChain;
  ComPtr<ID3D11RenderTargetView> renderTarget;
  NOTIFYICONDATAW tray{};
  bool visible = true;
  bool quit = false;
};

App g_app;

void CreateRenderTarget() {
  ComPtr<ID3D11Texture2D> back;
  g_app.swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
  g_app.d3d.device->CreateRenderTargetView(back.Get(), nullptr, &g_app.renderTarget);
}

bool CreateSwapChain() {
  ComPtr<IDXGIDevice> dxgiDevice;
  ComPtr<IDXGIAdapter> adapter;
  ComPtr<IDXGIFactory2> factory;
  if (FAILED(g_app.d3d.device.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(&adapter)) ||
      FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
    return false;
  }
  DXGI_SWAP_CHAIN_DESC1 desc{};
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  if (FAILED(factory->CreateSwapChainForHwnd(g_app.d3d.device.Get(), g_app.hwnd, &desc, nullptr, nullptr,
                                             &g_app.swapChain))) {
    return false;
  }
  CreateRenderTarget();
  return true;
}

void ShowFromTray() {
  ShowWindow(g_app.hwnd, SW_SHOW);
  SetForegroundWindow(g_app.hwnd);
  g_app.visible = true;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return TRUE;
  switch (msg) {
    case WM_SIZE:
      if (g_app.swapChain && wParam != SIZE_MINIMIZED) {
        g_app.renderTarget.Reset();
        g_app.swapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
        CreateRenderTarget();
      }
      return 0;
    case WM_CLOSE:
      // Closing hides to the tray; the camera keeps working.
      ShowWindow(hwnd, SW_HIDE);
      g_app.visible = false;
      return 0;
    case kTrayMessage:
      if (LOWORD(lParam) == WM_LBUTTONDBLCLK) ShowFromTray();
      if (LOWORD(lParam) == WM_RBUTTONUP) {
        POINT cursor;
        GetCursorPos(&cursor);
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, kTrayOpen, L"Открыть");
        AppendMenuW(menu, MF_STRING, kTrayExit, L"Выход");
        SetForegroundWindow(hwnd);
        const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, cursor.x, cursor.y, 0, hwnd, nullptr);
        DestroyMenu(menu);
        if (command == kTrayOpen) ShowFromTray();
        if (command == kTrayExit) g_app.quit = true;
      }
      return 0;
    case WM_DESTROY:
      g_app.quit = true;
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

std::string Utf8(const std::wstring& text) {
  if (text.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0, nullptr, nullptr);
  std::string out(size_t(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), out.data(), size, nullptr, nullptr);
  return out;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  auto single = AcquireSingleInstance();
  if (!single) {
    MessageBoxW(nullptr, L"Webicam уже запущен.", L"Webicam", MB_ICONINFORMATION);
    return 0;
  }

  std::optional<std::pair<std::string, uint16_t>> direct;
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::wstring(argv[i]) == L"--direct") {
      const std::wstring value = argv[i + 1];
      const auto colon = value.rfind(L':');
      if (colon != std::wstring::npos) direct = {Utf8(value.substr(0, colon)), uint16_t(_wtoi(value.c_str() + colon + 1))};
    }
  }
  LocalFree(argv);

  EnsureMediaFoundation();
  auto d3d = D3DContext::Create();
  if (!d3d) {
    MessageBoxW(nullptr, L"Не удалось создать Direct3D 11 устройство.", L"Webicam", MB_ICONERROR);
    return 1;
  }
  g_app.d3d = *d3d;

  WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, WndProc, 0, 0, instance, LoadIconW(nullptr, IDI_APPLICATION),
                 LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr, L"WebicamWindow", nullptr};
  RegisterClassExW(&wc);
  g_app.hwnd = CreateWindowExW(0, wc.lpszClassName, L"Webicam", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                               1280, 760, nullptr, nullptr, instance, nullptr);
  if (!CreateSwapChain()) {
    MessageBoxW(nullptr, L"Не удалось создать окно Direct3D.", L"Webicam", MB_ICONERROR);
    return 1;
  }
  ShowWindow(g_app.hwnd, SW_SHOWDEFAULT);

  g_app.tray.cbSize = sizeof(g_app.tray);
  g_app.tray.hWnd = g_app.hwnd;
  g_app.tray.uID = 1;
  g_app.tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  g_app.tray.uCallbackMessage = kTrayMessage;
  g_app.tray.hIcon = wc.hIcon;
  wcscpy_s(g_app.tray.szTip, L"Webicam");
  Shell_NotifyIconW(NIM_ADD, &g_app.tray);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  wchar_t fontsDir[MAX_PATH];
  GetWindowsDirectoryW(fontsDir, MAX_PATH);
  const std::string font = Utf8(std::wstring(fontsDir) + L"\\Fonts\\segoeui.ttf");
  if (!io.Fonts->AddFontFromFileTTF(font.c_str(), 18.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic())) {
    io.Fonts->AddFontDefault();
  }
  ImGui::StyleColorsDark();
  ImGui_ImplWin32_Init(g_app.hwnd);
  ImGui_ImplDX11_Init(g_app.d3d.device.Get(), g_app.d3d.context.Get());

  app::UiState ui;
  VirtualCamHost camera;
  if (!camera.Start()) {
    ui.cameraError = "Виртуальная камера не запущена (" + Utf8(camera.LastError()) +
                     "). Запустите install.ps1 от имени администратора.";
  }

  // The pipeline's callbacks reach the connection through a raw pointer that
  // stays valid until after connection->Stop() has joined every thread.
  std::unique_ptr<ConnectionManager> connection;
  ConnectionManager* connectionPtr = nullptr;
  Pipeline pipeline(
      g_app.d3d, [&] { connectionPtr->RequestKeyframe(); },
      [&](uint64_t phoneUs) { return connectionPtr->PhoneToPcUs(phoneUs); });
  if (!pipeline.Ready()) ui.cameraError = "Нет аппаратного декодера H.264 (D3D11).";

  std::mutex statusMutex;
  std::string phoneStatus;
  connection = std::make_unique<ConnectionManager>(ConnectionManager::Callbacks{
      .onFrame = [&](proto::VideoFrame&& frame) { pipeline.OnFrame(std::move(frame)); },
      .onConnectionChanged =
          [&](bool connected) {
            pipeline.OnConnectionChanged(connected);
            if (!connected) {
              std::lock_guard lock(statusMutex);
              phoneStatus.clear();
            }
          },
      .onStatus =
          [&](const proto::Status& status) {
            pipeline.SetPhoneStreaming(status.state == proto::StreamState::Streaming ||
                                       status.state == proto::StreamState::ThermalThrottled);
            std::lock_guard lock(statusMutex);
            switch (status.state) {
              case proto::StreamState::Streaming: phoneStatus.clear(); break;
              case proto::StreamState::StoppedByUser: phoneStatus = "Трансляция остановлена на iPhone"; break;
              case proto::StreamState::ThermalThrottled:
                phoneStatus = "iPhone перегревается: качество снижено";
                break;
              case proto::StreamState::Error: phoneStatus = "Ошибка на iPhone: " + status.message; break;
            }
          },
  });
  connectionPtr = connection.get();
  if (direct) connection->SetDirect(direct->first, direct->second);
  connection->Start();

  while (!g_app.quit) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (g_app.quit) break;
    pipeline.Tick();
    if (!g_app.visible || IsIconic(g_app.hwnd)) {
      Sleep(50);
      continue;
    }
    {
      std::lock_guard lock(statusMutex);
      ui.phoneStatus = phoneStatus;
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    app::DrawUi(ui, *connection, pipeline);
    ImGui::Render();
    const float clear[4] = {0.08f, 0.08f, 0.09f, 1.0f};
    g_app.d3d.context->OMSetRenderTargets(1, g_app.renderTarget.GetAddressOf(), nullptr);
    g_app.d3d.context->ClearRenderTargetView(g_app.renderTarget.Get(), clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_app.swapChain->Present(1, 0);
  }

  connection->Stop();
  connection.reset();
  camera.Stop();
  Shell_NotifyIconW(NIM_DELETE, &g_app.tray);
  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
  DestroyWindow(g_app.hwnd);
  CloseHandle(*single);
  return 0;
}
