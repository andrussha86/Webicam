file(GLOB WEBICAM_CORE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(webicam_core STATIC ${WEBICAM_CORE_SOURCES} include/webicam/version.h)
target_include_directories(webicam_core PUBLIC include)
target_link_libraries(webicam_core PUBLIC mfplat mfuuid mfreadwrite mf mfsensorgroup d3d11 dxgi ws2_32 ole32 propsys advapi32)
