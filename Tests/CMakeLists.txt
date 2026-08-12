cmake_minimum_required(VERSION 3.16)
project(g0b1_apu_tests C)
set(CMAKE_C_STANDARD 11)
enable_testing()

add_library(unity STATIC unity/unity.c)
target_include_directories(unity PUBLIC unity)

# Include dirs for portable App code under test.
set(APP_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/../App/include
                ${CMAKE_CURRENT_SOURCE_DIR}/../App/services)

# Helper to register a test executable.
function(add_unity_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} unity)
  target_include_directories(${name} PRIVATE ${APP_INCLUDE})
  target_compile_options(${name} PRIVATE -Wall -Wextra -Werror -funsigned-char)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_unity_test(test_smoke test_smoke.c)
