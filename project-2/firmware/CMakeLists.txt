cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w)
include(pico_sdk_import.cmake)

project(rma2_firmware C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(rma2_firmware
  src/main.cpp
  ${ROOT}/shared/rma2/device.cpp
  ${ROOT}/shared/rma2/knob.cpp
  ${ROOT}/shared/rma2/melodies.cpp
  ${ROOT}/shared/rma2/sequencer.cpp
)
target_include_directories(rma2_firmware PRIVATE ${ROOT}/shared)
target_compile_options(rma2_firmware PRIVATE -Wall -Wextra)
target_compile_definitions(rma2_firmware PRIVATE
  # Bound how long a blocked printf can stall the loop (sound timing, watchdog).
  PICO_STDIO_USB_STDOUT_TIMEOUT_US=20000
  RMA_FW_VERSION="0.2.0"
)
target_link_libraries(rma2_firmware pico_stdlib hardware_pwm hardware_adc hardware_watchdog)

pico_enable_stdio_usb(rma2_firmware 1)
pico_enable_stdio_uart(rma2_firmware 0)
pico_add_extra_outputs(rma2_firmware)
