cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w)
include(pico_sdk_import.cmake)

project(rma3_firmware C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(rma3_firmware
  src/main.cpp
  ${ROOT}/shared/rma3/device.cpp
  ${ROOT}/shared/rma3/knob.cpp
  ${ROOT}/shared/rma3/melodies.cpp
  ${ROOT}/shared/rma3/schedule.cpp
  ${ROOT}/shared/rma3/sequencer.cpp
  ${ROOT}/shared/rma3/wifi.cpp
)
target_include_directories(rma3_firmware PRIVATE ${ROOT}/shared ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(rma3_firmware PRIVATE -Wall -Wextra)
target_compile_definitions(rma3_firmware PRIVATE
  # Bound how long a blocked printf can stall the loop (sound timing, watchdog).
  PICO_STDIO_USB_STDOUT_TIMEOUT_US=20000
  RMA_FW_VERSION="0.3.0"
  CYW43_HOST_NAME="rma"
)
target_link_libraries(rma3_firmware pico_stdlib hardware_pwm hardware_adc hardware_watchdog
  hardware_flash pico_flash pico_cyw43_arch_lwip_poll pico_lwip_mdns)

pico_enable_stdio_usb(rma3_firmware 1)
pico_enable_stdio_uart(rma3_firmware 0)
pico_add_extra_outputs(rma3_firmware)
