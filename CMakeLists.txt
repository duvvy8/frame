# Frame — CEF/C++ rewrite.
#
# Migration step 2 scope: pure shared logic plus its tests. There is
# deliberately NO CEF dependency in this file yet — the layout math is
# framework-free, so it builds and tests without the CEF binary distribution
# being present or pinned. CEF enters the build at step 3, when the first OSR
# chrome surface is stood up.
#
# No Node.js. No Electron. Not at build time, not at runtime.

cmake_minimum_required(VERSION 3.21)

project(frame
  VERSION 0.1.0
  DESCRIPTION "Frame browser — CEF/C++ engine"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra)
endif()

# Header-only shared layer: constants and pure geometry that the browser
# process, every OSR chrome surface, and the corner-mask code all agree on.
add_library(frame_shared INTERFACE)
target_include_directories(frame_shared INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(frame_shared INTERFACE cxx_std_17)

option(FRAME_BUILD_TESTS "Build Frame's unit tests" ON)

if(FRAME_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
