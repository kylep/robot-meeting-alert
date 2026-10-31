cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w)
include(pico_sdk_import.cmake)

project(rma_firmware C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(rma_firmware
  src/main.cpp
  ${ROOT}/shared/rma/device.cpp
)
target_include_directories(rma_firmware PRIVATE ${ROOT}/shared)
target_compile_options(rma_firmware PRIVATE -Wall -Wextra)
target_compile_definitions(rma_firmware PRIVATE
  # Bound how long a blocked printf can delay buzzer lease enforcement.
  PICO_STDIO_USB_STDOUT_TIMEOUT_US=20000
  RMA_FW_VERSION="0.1.0"
)
target_link_libraries(rma_firmware pico_stdlib)

pico_enable_stdio_usb(rma_firmware 1)
pico_enable_stdio_uart(rma_firmware 0)
pico_add_extra_outputs(rma_firmware)
