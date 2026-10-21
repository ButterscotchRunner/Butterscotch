cmake_minimum_required(VERSION 3.20)
project(ButterscotchPreprocessorC VERSION 1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(PNG REQUIRED)
find_package(BZip2 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(VORBIS REQUIRED IMPORTED_TARGET vorbisfile)
pkg_check_modules(PCRE2 REQUIRED IMPORTED_TARGET libpcre2-8)

option(ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

add_library(processor STATIC
    io.c
    datawin.c
    image.c
    audio.c
    pipeline.c
)

target_include_directories(processor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(processor PUBLIC _POSIX_C_SOURCE=200809L)
if(APPLE)
  target_compile_definitions(processor PUBLIC _DARWIN_C_SOURCE)
endif()
target_compile_options(processor PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(processor PUBLIC PNG::PNG BZip2::BZip2 PkgConfig::VORBIS PkgConfig::PCRE2 m)

if(ENABLE_SANITIZERS)
  target_compile_options(processor PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(processor PUBLIC -fsanitize=address,undefined)
endif()
