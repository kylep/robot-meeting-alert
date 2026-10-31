cmake_minimum_required(VERSION 3.20)
project(rma_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_library(rma_shared STATIC ${ROOT}/shared/rma/device.cpp ${ROOT}/shared/rma/host.cpp)
target_include_directories(rma_shared PUBLIC ${ROOT}/shared)

enable_testing()

add_executable(test_device test_device.cpp)
target_link_libraries(test_device rma_shared)
add_test(NAME test_device COMMAND test_device)

add_executable(test_host test_host.cpp)
target_link_libraries(test_host rma_shared)
add_test(NAME test_host COMMAND test_host)

add_executable(test_link test_link.cpp ${ROOT}/mac/src/serial_port.cpp ${ROOT}/mac/src/link.cpp)
target_include_directories(test_link PRIVATE ${ROOT}/mac/src)
target_link_libraries(test_link rma_shared)
add_test(NAME test_link COMMAND test_link)
set_tests_properties(test_link PROPERTIES TIMEOUT 30)
