add_library(webicam_vcam SHARED dllmain.cpp media_source.cpp media_stream.cpp overlay.cpp webicam_vcam.def)
target_link_libraries(webicam_vcam PRIVATE webicam_core mfplat mfuuid strmiids gdi32 runtimeobject)
