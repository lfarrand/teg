cmake_minimum_required(VERSION 3.20)
project(teg_parser_fuzzers LANGUAGES C CXX)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "The fuzz targets require Clang/libFuzzer")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(SANITIZERS -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
set(LIB_SANITIZERS -fsanitize=address,undefined -fno-omit-frame-pointer)

add_library(teg-miniz STATIC ../lib/miniz/miniz.c)
target_include_directories(teg-miniz PUBLIC ../lib/miniz)
target_compile_definitions(teg-miniz PRIVATE
  MINIZ_NO_ARCHIVE_APIS MINIZ_NO_STDIO MINIZ_NO_TIME)
target_compile_options(teg-miniz PRIVATE ${LIB_SANITIZERS})
target_link_options(teg-miniz PRIVATE -fsanitize=address,undefined)

function(add_teg_fuzzer target source)
  add_executable(${target} ${source})
  target_include_directories(${target} PRIVATE ../src ../lib/miniz)
  target_compile_definitions(${target} PRIVATE
    MINIZ_NO_ARCHIVE_APIS MINIZ_NO_STDIO MINIZ_NO_TIME)
  target_compile_options(${target} PRIVATE ${SANITIZERS} -Wall -Wextra)
  target_link_options(${target} PRIVATE ${SANITIZERS})
endfunction()

add_teg_fuzzer(ota-ingest-fuzzer ota_ingest_fuzzer.cpp)
add_teg_fuzzer(waveform-fuzzer waveform_fuzzer.cpp)
add_teg_fuzzer(gzip-fuzzer gzip_fuzzer.cpp)
target_link_libraries(gzip-fuzzer PRIVATE teg-miniz)
