# Frame application target.
#
# CEF's CMake config is pulled in HERE rather than at the top level on purpose.
# find_package(CEF) sets compiler flags for its directory scope — /MT, /GR-,
# and _HAS_EXCEPTIONS=0 among them — and Catch2 requires exceptions. Keeping
# CEF scoped to this subdirectory stops those flags reaching the test target.

set(CEF_ROOT "${CMAKE_SOURCE_DIR}/third_party/cef")
list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")

find_package(CEF REQUIRED)

# Builds the C++ convenience layer over CEF's C API.
add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}" libcef_dll_wrapper)

set(FRAME_SRCS
  frame_app.cc
  frame_app.h
  main_window.cc
  main_window.h
  page_client.cc
  page_client.h
  chrome_surface.cc
  chrome_surface.h
  "${CMAKE_SOURCE_DIR}/src/frame_main_win.cc"
)
source_group(frame FILES ${FRAME_SRCS})

set(CEF_TARGET "frame")
ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
SET_CEF_TARGET_OUT_DIR()

if(USE_SANDBOX)
  # With the sandbox enabled CEF 151 builds the app as a DLL and launches it
  # through its own bootstrap executable, which calls our exported RunWinMain.
  # The sandbox stays ON: a sandboxed renderer with no Node-equivalent to reach
  # is the architectural reason for this rewrite, so it is not traded away for
  # a simpler build.
  add_library(${CEF_TARGET} SHARED ${FRAME_SRCS})
  SET_LIBRARY_TARGET_PROPERTIES(${CEF_TARGET})
  COPY_SINGLE_FILE(${CEF_TARGET} "${CEF_BINARY_DIR}/bootstrap.exe"
                   "${CEF_TARGET_OUT_DIR}/${CEF_TARGET}.exe")
  if(CMAKE_GENERATOR MATCHES "Visual Studio")
    set_target_properties(${CEF_TARGET} PROPERTIES
      VS_DEBUGGER_COMMAND "$<TARGET_FILE_DIR:${CEF_TARGET}>/${CEF_TARGET}.exe")
  endif()
else()
  add_executable(${CEF_TARGET} WIN32 ${FRAME_SRCS})
  SET_EXECUTABLE_TARGET_PROPERTIES(${CEF_TARGET})
endif()

add_dependencies(${CEF_TARGET} libcef_dll_wrapper)
target_link_libraries(${CEF_TARGET} libcef_lib libcef_dll_wrapper
                      ${CEF_STANDARD_LIBS}
                      dwmapi)

# Our own headers ("browser/...", "shared/...") alongside CEF's.
target_include_directories(${CEF_TARGET} PRIVATE "${CMAKE_SOURCE_DIR}/src")

# CEF's own binaries and .pak resources.
COPY_FILES("${CEF_TARGET}" "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}"
           "${CEF_TARGET_OUT_DIR}")
COPY_FILES("${CEF_TARGET}" "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}"
           "${CEF_TARGET_OUT_DIR}")

# Chrome surface HTML/CSS, staged next to the executable. Re-copied on every
# build so editing a surface only needs a rebuild, not a reconfigure.
add_custom_command(TARGET ${CEF_TARGET} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_SOURCE_DIR}/src/renderer"
          "${CEF_TARGET_OUT_DIR}/resources"
  COMMENT "Staging chrome surfaces"
  VERBATIM)

if(USE_SANDBOX)
  SET_LPAC_ACLS("${CEF_TARGET}")
endif()
