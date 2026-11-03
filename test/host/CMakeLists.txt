# Host-side unit tests (no ESP-IDF required).
#
#   cmake -S test/host -B build-host-tests
#   cmake --build build-host-tests
#   ctest --test-dir build-host-tests --output-on-failure
#
# RelayController is compiled against the fake GPIO / FreeRTOS / logging headers in stubs/.
# ZigbeeHelpers.hpp has no platform dependencies and is tested directly.
cmake_minimum_required(VERSION 3.22)
project(ZigbeeGarageDoorOpenerHostTests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON) # stubs use the GNU ##__VA_ARGS__ extension

add_executable(host_tests
    test_main.cpp
    test_zigbee_helpers.cpp
    test_relay_controller.cpp
    test_door_sensor.cpp
)

target_include_directories(host_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs   # fake ESP-IDF headers; must win over real ones
    ${CMAKE_CURRENT_SOURCE_DIR}/../../main
)

target_compile_options(host_tests PRIVATE -Wall -Wextra -Wconversion -Wno-unused-parameter
                                          -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(host_tests PRIVATE -fsanitize=address,undefined)

enable_testing()
add_test(NAME host_tests COMMAND host_tests)
# One test intentionally never runs the pulse worker (see test_relay_controller.cpp).
set_tests_properties(host_tests PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
