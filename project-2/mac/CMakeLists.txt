cmake_minimum_required(VERSION 3.24)
project(robot_meeting_alert_mac2 C CXX OBJC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_library(rma_core STATIC
  ${ROOT}/shared/rma2/host.cpp
  src/serial_port.cpp
  src/link.cpp
  src/discovery.cpp)
target_include_directories(rma_core PUBLIC ${ROOT}/shared src)
target_compile_options(rma_core PRIVATE -Wall -Wextra)
target_link_libraries(rma_core PUBLIC "-framework IOKit" "-framework CoreFoundation")

add_executable(rma_app MACOSX_BUNDLE src/app_main.cpp)
target_compile_options(rma_app PRIVATE -Wall -Wextra)
target_link_libraries(rma_app PRIVATE rma_core imgui)
set_target_properties(rma_app PROPERTIES
  OUTPUT_NAME "Robot Meeting Alert"
  MACOSX_BUNDLE_BUNDLE_NAME "Robot Meeting Alert"
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.kylep.robot-meeting-alert.project2"
  MACOSX_BUNDLE_BUNDLE_VERSION "0.2.0"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "0.2.0")

add_executable(rmactl src/rmactl.cpp)
target_compile_options(rmactl PRIVATE -Wall -Wextra)
target_link_libraries(rmactl PRIVATE rma_core)
