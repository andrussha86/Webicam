find_package(imgui CONFIG REQUIRED)
add_executable(webicam WIN32 main.cpp ui.cpp theme.cpp webicam.rc)
target_link_libraries(webicam PRIVATE webicam_core imgui::imgui dxgi d3d11 dwmapi shell32)
