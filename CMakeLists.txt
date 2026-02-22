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

# Active Ymir tooling targets: keep these discoverable for offline-first timing work.
add_library(busarb STATIC
  src/busarb/busarb.cpp
  src/busarb/ymir_timing.cpp
)
target_include_directories(busarb PUBLIC include)

# Legacy/experimental scaffold target retained for harness coverage and trace generation.
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

# Legacy harness executable (not the primary roadmap deliverable).
add_executable(saturnemu src/main.cpp)
target_link_libraries(saturnemu PRIVATE saturnis_core)

# Active comparative replay CLI for offline-first analysis.
add_executable(trace_replay tools/trace_replay/trace_replay.cpp)
target_link_libraries(trace_replay PRIVATE busarb)

include(CTest)
if(BUILD_TESTING)
  # Prioritized for Ymir tool workflow: deterministic busarb/replay/timing tests.
  # Legacy scaffold validation tests retained for deterministic harness behavior.
  add_executable(saturnis_kernel_tests tests/test_kernel.cpp)
  target_link_libraries(saturnis_kernel_tests PRIVATE saturnis_core)
  add_test(NAME saturnis_kernel_tests COMMAND saturnis_kernel_tests)

  add_executable(saturnis_trace_regression tests/test_trace_regression.cpp)
  target_link_libraries(saturnis_trace_regression PRIVATE saturnis_core)
  add_test(NAME saturnis_trace_regression COMMAND saturnis_trace_regression)

  # Active deterministic arbitration/replay correctness tests.
  add_executable(busarb_tests tests/test_busarb.cpp)
  target_link_libraries(busarb_tests PRIVATE busarb)
  add_test(NAME busarb_tests COMMAND busarb_tests)

  add_executable(busarb_link_harness tests/test_busarb_link_harness.cpp)
  target_link_libraries(busarb_link_harness PRIVATE busarb)
  add_test(NAME busarb_link_harness COMMAND busarb_link_harness)

  add_executable(ymir_timing_tests tests/test_ymir_timing.cpp)
  target_link_libraries(ymir_timing_tests PRIVATE busarb)
  add_test(NAME ymir_timing_tests COMMAND ymir_timing_tests)

  add_test(
    NAME trace_replay_sample
    COMMAND trace_replay ${CMAKE_SOURCE_DIR}/tests/fixtures/trace_replay/sample_trace.jsonl
            --annotated-output ${CMAKE_BINARY_DIR}/trace_replay_annotated.jsonl --top 5
  )
  set_tests_properties(trace_replay_sample PROPERTIES
    PASS_REGULAR_EXPRESSION "records_processed: 10;malformed_lines_skipped: 1;known_gap_count:"
  )

  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(
      NAME saturnis_bios_metrics_scripts
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_bios_metrics_scripts.py
    )
  endif()
endif()
