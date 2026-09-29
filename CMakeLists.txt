cmake_minimum_required(VERSION 3.25)
project(chronos_ml LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(APPLE)
  # Workaround for a broken/truncated bundled libc++ in some Xcode Command
  # Line Tools installs, which shadows the real SDK headers (e.g. <regex>
  # fails to resolve). Force the SDK's libc++ ahead of it.
  execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE _sdk_path OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -isystem ${_sdk_path}/usr/include/c++/v1")
endif()

include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.6.0
)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.9.0
)

FetchContent_MakeAvailable(Catch2 benchmark)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

add_library(chronos_core INTERFACE)
target_include_directories(chronos_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(chronos_core INTERFACE cxx_std_20)
target_compile_options(chronos_core INTERFACE
  -O3 -march=native -fno-omit-frame-pointer -Wall -Wextra -Wpedantic
)

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
add_subdirectory(benchmarks)
