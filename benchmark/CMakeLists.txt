cmake_minimum_required(VERSION 3.20)
project(teg_host_benchmarks LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

include(FetchContent)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  googlebenchmark
  URL https://github.com/google/benchmark/archive/refs/tags/v1.9.5.tar.gz
  URL_HASH SHA256=9631341c82bac4a288bef951f8b26b41f69021794184ece969f8473977eaa340
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
FetchContent_MakeAvailable(googlebenchmark)

add_executable(teg-benchmarks teg_benchmarks.cpp)
target_include_directories(teg-benchmarks PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_compile_options(teg-benchmarks PRIVATE -O2 -Wall -Wextra)
target_link_libraries(teg-benchmarks PRIVATE benchmark::benchmark)
