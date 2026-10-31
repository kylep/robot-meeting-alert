cmake_minimum_required(VERSION 3.20)
project(rma2_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_library(rma2_shared STATIC
  ${ROOT}/shared/rma2/device.cpp
  ${ROOT}/shared/rma2/host.cpp
  ${ROOT}/shared/rma2/knob.cpp
  ${ROOT}/shared/rma2/melodies.cpp
  ${ROOT}/shared/rma2/sequencer.cpp)
target_include_directories(rma2_shared PUBLIC ${ROOT}/shared)

enable_testing()

add_executable(test_device test_device.cpp)
target_link_libraries(test_device rma2_shared)
add_test(NAME test_device COMMAND test_device)

add_executable(test_link test_link.cpp ${ROOT}/mac/src/serial_port.cpp ${ROOT}/mac/src/link.cpp)
target_include_directories(test_link PRIVATE ${ROOT}/mac/src)
target_link_libraries(test_link rma2_shared)
add_test(NAME test_link COMMAND test_link)
set_tests_properties(test_link PROPERTIES TIMEOUT 30)
