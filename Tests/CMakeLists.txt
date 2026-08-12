cmake_minimum_required(VERSION 3.16)
project(g0b1_apu_tests C)
set(CMAKE_C_STANDARD 11)
enable_testing()

add_library(unity STATIC unity/unity.c)
target_include_directories(unity PUBLIC unity)

# Include dirs for portable App code under test.
set(APP_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/../App/include
                ${CMAKE_CURRENT_SOURCE_DIR}/../App/services
                ${CMAKE_CURRENT_SOURCE_DIR}/fakes)

# Helper to register a test executable.
function(add_unity_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} unity)
  target_include_directories(${name} PRIVATE ${APP_INCLUDE})
  target_compile_options(${name} PRIVATE -Wall -Wextra -Werror -funsigned-char)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_unity_test(test_smoke test_smoke.c)
add_unity_test(test_types test_types.c)
add_unity_test(test_modbus_crc test_modbus_crc.c ../App/services/modbus_crc.c)
add_unity_test(test_modbus_frame test_modbus_frame.c ../App/services/modbus_frame.c ../App/services/modbus_crc.c)
add_unity_test(test_modbus_read test_modbus_read.c ../App/services/modbus_read.c ../App/services/modbus_crc.c)
add_unity_test(test_fake_nor test_fake_nor.c fakes/fake_nor.c)
add_unity_test(test_nvm_defaults test_nvm_defaults.c ../App/services/nvm_defaults.c)
