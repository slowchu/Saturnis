cmake_minimum_required(VERSION 3.20)
project(Saturnis LANGUAGES CXX)

option(SATURNIS_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(SATURNIS_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(SATURNIS_ENABLE_X86_MICROKERNEL "Enable optional x86-64 microkernel stubs" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wnon-virtual-dtor)
endif()

if(SATURNIS_ENABLE_ASAN AND NOT MSVC)
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address)
endif()

if(SATURNIS_ENABLE_UBSAN AND NOT MSVC)
  add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=undefined)
endif()

find_package(SDL2 QUIET)


add_library(busarb STATIC
  src/busarb/busarb.cpp
)
target_include_directories(busarb PUBLIC include)

add_library(saturnis_core
  src/core/trace.cpp
  src/core/time.cpp
  src/bus/bus_arbiter.cpp
  src/mem/memory.cpp
  src/cpu/scripted_cpu.cpp
  src/cpu/sh2_core.cpp
  src/cpu/sh2_decode.cpp
  src/dev/devices.cpp
  src/platform/file_io.cpp
  src/platform/sdl_window.cpp
  src/core/emulator.cpp
)

target_include_directories(saturnis_core PUBLIC src)

if(SDL2_FOUND)
  target_compile_definitions(saturnis_core PUBLIC SATURNIS_HAS_SDL2=1)
  target_link_libraries(saturnis_core PUBLIC SDL2::SDL2)
else()
  target_compile_definitions(saturnis_core PUBLIC SATURNIS_HAS_SDL2=0)
endif()

if(SATURNIS_ENABLE_X86_MICROKERNEL)
  target_compile_definitions(saturnis_core PUBLIC SATURNIS_ENABLE_X86_MICROKERNEL=1)
else()
  target_compile_definitions(saturnis_core PUBLIC SATURNIS_ENABLE_X86_MICROKERNEL=0)
endif()

add_executable(saturnemu src/main.cpp)
target_link_libraries(saturnemu PRIVATE saturnis_core)

include(CTest)
if(BUILD_TESTING)
  add_executable(saturnis_kernel_tests tests/test_kernel.cpp)
  target_link_libraries(saturnis_kernel_tests PRIVATE saturnis_core)
  add_test(NAME saturnis_kernel_tests COMMAND saturnis_kernel_tests)

  add_executable(saturnis_trace_regression tests/test_trace_regression.cpp)
  target_link_libraries(saturnis_trace_regression PRIVATE saturnis_core)
  add_test(NAME saturnis_trace_regression COMMAND saturnis_trace_regression)

  add_executable(busarb_tests tests/test_busarb.cpp)
  target_link_libraries(busarb_tests PRIVATE busarb)
  add_test(NAME busarb_tests COMMAND busarb_tests)

  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(
      NAME saturnis_bios_metrics_scripts
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_bios_metrics_scripts.py
    )
  endif()
endif()
