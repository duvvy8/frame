# Catch2 chosen over GoogleTest purely on stand-up cost: one FetchContent block,
# one linked target that provides its own main(), no separate gmock decisions.
#
# NOTE: this downloads Catch2 at configure time and needs network access on the
# first configure. It is cached in the build tree thereafter.
include(FetchContent)

FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.2
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Catch2)

add_executable(frame_tests
  chrome_layout_test.cpp
  url_util_test.cpp
  internal_pages_test.cpp
  shortcuts_test.cpp
  tracker_filter_test.cpp
  sleep_policy_test.cpp
)

target_link_libraries(frame_tests
  PRIVATE
    frame_shared
    Catch2::Catch2WithMain
)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(frame_tests)
