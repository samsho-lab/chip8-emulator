cmake_minimum_required(VERSION 3.20)
project(chip8 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Core library: no I/O, no SDL. Everything platform-specific lives in the frontends.
add_library(chip8core src/Chip8.cpp)
target_include_directories(chip8core PUBLIC include)

add_executable(chip8-headless src/main_headless.cpp)
target_link_libraries(chip8-headless PRIVATE chip8core)

find_package(SDL2 QUIET)
if(SDL2_FOUND)
  add_executable(chip8 src/main_sdl.cpp)
  target_link_libraries(chip8 PRIVATE chip8core SDL2::SDL2 SDL2::SDL2main)
else()
  message(STATUS "SDL2 not found: building only the headless runner")
endif()

enable_testing()
add_executable(chip8-tests tests/test_chip8.cpp)
target_link_libraries(chip8-tests PRIVATE chip8core)
add_test(NAME chip8-tests COMMAND chip8-tests)
