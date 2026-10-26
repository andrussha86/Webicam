add_executable(vcam-probe main.cpp)
target_link_libraries(vcam-probe PRIVATE webicam_core)
