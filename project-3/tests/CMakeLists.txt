cmake_minimum_required(VERSION 3.20)
project(rma3_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_library(rma3_shared STATIC
  ${ROOT}/shared/rma3/device.cpp
  ${ROOT}/shared/rma3/host.cpp
  ${ROOT}/shared/rma3/knob.cpp
  ${ROOT}/shared/rma3/melodies.cpp
  ${ROOT}/shared/rma3/schedule.cpp
  ${ROOT}/shared/rma3/sequencer.cpp
  ${ROOT}/shared/rma3/wifi.cpp)
target_include_directories(rma3_shared PUBLIC ${ROOT}/shared)

enable_testing()

add_executable(test_v3 test_v3.cpp)
target_link_libraries(test_v3 rma3_shared)
add_test(NAME test_v3 COMMAND test_v3)

add_executable(test_device test_device.cpp)
target_link_libraries(test_device rma3_shared)
add_test(NAME test_device COMMAND test_device)

add_executable(test_link test_link.cpp ${ROOT}/mac/src/serial_port.cpp ${ROOT}/mac/src/link.cpp)
target_include_directories(test_link PRIVATE ${ROOT}/mac/src)
target_link_libraries(test_link rma3_shared)
add_test(NAME test_link COMMAND test_link)
set_tests_properties(test_link PROPERTIES TIMEOUT 30)
