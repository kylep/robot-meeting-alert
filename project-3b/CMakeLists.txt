# Project 3b: the project 3 app and rmactl for Windows (experimental).
# Reuses ../project-3 by path: shared/rma3 (protocol, schedule, iCal reader),
# and the portable parts of mac/src (the UI, the robot link, rmactl, the iCal
# link source). Only the pieces that talk to the operating system are new,
# in src/: COM ports, Winsock, rma.local lookup, key storage, WinHTTP, Outlook.
cmake_minimum_required(VERSION 3.24)
project(robot_meeting_alert_win C CXX)

if(NOT WIN32)
  message(FATAL_ERROR "Project 3b builds for Windows only. From a Mac or Linux, cross-compile with "
                      "-DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake (see README.md).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# One self-contained .exe: no Visual C++ or MinGW runtime DLLs to install.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.4.18
  GIT_SHALLOW TRUE)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.93.0
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(SDL3 imgui)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3-static)

set(P3 ${CMAKE_CURRENT_SOURCE_DIR}/../project-3)
# Our code only (SDL and Dear ImGui keep their own settings): Windows 10 APIs.
add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
  set(WARN /W3 /utf-8)
else()
  set(WARN -Wall -Wextra)
  add_link_options(-static $<$<CONFIG:Release>:-s>)  # one .exe with libstdc++ etc. inside; stripped
endif()

add_library(rma3_shared STATIC
  ${P3}/shared/rma3/auth.cpp
  ${P3}/shared/rma3/device.cpp
  ${P3}/shared/rma3/host.cpp
  ${P3}/shared/rma3/ics.cpp
  ${P3}/shared/rma3/knob.cpp
  ${P3}/shared/rma3/melodies.cpp
  ${P3}/shared/rma3/schedule.cpp
  ${P3}/shared/rma3/sequencer.cpp
  ${P3}/shared/rma3/wifi.cpp)
target_include_directories(rma3_shared PUBLIC ${P3}/shared)
target_link_libraries(rma3_shared PUBLIC bcrypt)

add_library(rma_core STATIC
  src/pairing_win.cpp
  src/serial_port_win.cpp
  src/discovery_win.cpp
  ${P3}/mac/src/link.cpp)
target_include_directories(rma_core PUBLIC ${P3}/mac/src)
target_compile_options(rma_core PRIVATE ${WARN})
target_link_libraries(rma_core PUBLIC rma3_shared ws2_32 setupapi advapi32 shell32 ole32)

add_executable(rma_app WIN32
  ${P3}/mac/src/app_main.cpp
  ${P3}/mac/src/ics_provider.cpp
  src/outlook_calendar.cpp
  src/http_win.cpp)
target_compile_options(rma_app PRIVATE ${WARN})
target_link_libraries(rma_app PRIVATE rma_core imgui winhttp oleaut32 uuid)
set_target_properties(rma_app PROPERTIES OUTPUT_NAME "RobotMeetingAlert")

add_executable(rmactl ${P3}/mac/src/rmactl.cpp)
target_compile_options(rmactl PRIVATE ${WARN})
target_link_libraries(rmactl PRIVATE rma_core)

# The host tests that don't need a pty (test_link does): protocol, device
# logic, schedule and the iCal reader.
enable_testing()
foreach(t test_v3 test_device test_ics)
  add_executable(${t} ${P3}/tests/${t}.cpp)
  target_link_libraries(${t} PRIVATE rma3_shared)
  add_test(NAME ${t} COMMAND ${t})
endforeach()
