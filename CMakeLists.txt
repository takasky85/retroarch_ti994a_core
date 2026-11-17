# TI-99/4A libretro core -- CMake (MSVC, MinGW-w64, clang, gcc)
#   cmake -S . -B build -A x64 && cmake --build build --config Release
cmake_minimum_required(VERSION 3.10)
project(ti99_libretro C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CORE_SOURCES
  src/cpu9900.c src/vdp9918.c src/psg76489.c src/speech.c src/osk.c src/ti99.c
  src/zipread.c src/loader.c src/libretro_core.c)

add_library(ti99_libretro SHARED ${CORE_SOURCES})
target_include_directories(ti99_libretro PRIVATE src third_party/libretro)
set_target_properties(ti99_libretro PROPERTIES PREFIX "")   # ti99_libretro.dll, not libti99_libretro.dll

if(MSVC)
  target_compile_definitions(ti99_libretro PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(ti99_libretro PRIVATE /W3)
else()
  target_compile_options(ti99_libretro PRIVATE -Wall -Wextra -fvisibility=hidden)
endif()

# Tests
enable_testing()
add_executable(test_ti99 tests/test_main.c src/cpu9900.c src/vdp9918.c src/psg76489.c src/speech.c src/osk.c
               src/ti99.c src/zipread.c src/loader.c)
target_include_directories(test_ti99 PRIVATE src third_party/libretro)
if(NOT MSVC)
  target_link_libraries(test_ti99 m)
else()
  target_compile_definitions(test_ti99 PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME unit COMMAND test_ti99 ${CMAKE_SOURCE_DIR}/tests/deflate_test.zip)
