file(GLOB WEBICAM_CORE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(webicam_core STATIC ${WEBICAM_CORE_SOURCES} include/webicam/version.h)
target_include_directories(webicam_core PUBLIC include)
target_link_libraries(webicam_core PUBLIC mfplat mfuuid mfreadwrite mf mfsensorgroup wmcodecdspuuid d3d11 dxgi ws2_32 ole32 propsys advapi32)

# Synthetic H.264 source for tests and fake-phone.
add_library(webicam_testing STATIC testing/test_encoder.cpp)
target_include_directories(webicam_testing PUBLIC testing)
target_link_libraries(webicam_testing PUBLIC webicam_core wmcodecdspuuid strmiids)
