cmake_minimum_required(VERSION 3.7)
project(faust-wavetables)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_definitions("-D_USE_MATH_DEFINES")
endif()

###
add_subdirectory("thirdparty/scope-lite")
add_subdirectory("thirdparty/span-lite")

###
add_library(kissfft STATIC EXCLUDE_FROM_ALL "thirdparty/kissfft/kiss_fft.c")
target_include_directories(kissfft PUBLIC "thirdparty/kissfft")

add_library(kissfftr STATIC EXCLUDE_FROM_ALL "thirdparty/kissfft/tools/kiss_fftr.c")
target_include_directories(kissfft PUBLIC "thirdparty/kissfft/tools")
target_link_libraries(kissfftr PUBLIC kissfft)

###
add_library(dr_wav INTERFACE)
target_include_directories(dr_wav INTERFACE "thirdparty/dr_libs")

###
add_executable(make-wavetable-faust
  "sources/main.cpp"
  "sources/dr_wav_library.c"
  "sources/dr_wav_library.h"
  "sources/sfizz/Wavetables.cpp"
  "sources/sfizz/Wavetables.h")
target_link_libraries(make-wavetable-faust PRIVATE kissfftr dr_wav nonstd::scope-lite nonstd::span-lite)
