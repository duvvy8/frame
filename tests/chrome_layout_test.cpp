// Parity tests for the chrome-layout port.
//
// Every expected value in this file was produced by executing the ORIGINAL
// Electron src/shared/chrome-layout.js under Node and recording its output —
// not by hand-computing what the C++ ought to return. That distinction is the
// point of the exercise: these assertions fail if the port drifts from the JS,
// including in the rounding/coercion corners where a "mechanical" translation
// silently diverges.
//
// Node was used once, at development time, as an oracle. It is not a build or
// runtime dependency of Frame and must never become one.

#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "shared/chrome_layout.h"

namespace {

using frame::layout::DynamicTabMax;
using frame::layout::TabMaxOptions;
using frame::layout::ViewportBounds;
using frame::layout::ViewportOptions;
using frame::layout::ViewportRect;

// Reads as: "the JS returned exactly this".
void ExpectRect(const ViewportRect& actual, int x, int y, int width, int height) {
  CHECK(actual.x == x);
  CHECK(actual.y == y);
  CHECK(actual.width == width);
  CHECK(actual.height == height);
  CHECK(actual.radius == 12);  // VIEWPORT_RADIUS, constant across every case
}

}  // namespace

TEST_CASE("Layout constants match the Electron build exactly", "[layout][constants]") {
  CHECK(frame::layout::kTopbarHeight == 32);
  CHECK(frame::layout::kSidebarWidth == 168);
  CHECK(frame::layout::kCollapsedRailWidth == 8);
  CHECK(frame::layout::kOuterInset == 0);
  CHECK(frame::layout::kOuterRadius == 0);
  CHECK(frame::layout::kShellInset == 8);
  CHECK(frame::layout::kViewportRadius == 12);
  CHECK(frame::layout::kBookmarksHeight == 30);
  CHECK(frame::layout::kSidebarTransitionMs == 210);
  CHECK(frame::layout::kTabMinWidth == 95);
  CHECK(frame::layout::kTabMaxWidth == 190);
  CHECK(frame::layout::kTabGap == 8);
  CHECK(frame::layout::kNewTabWidth == 34);
  CHECK(frame::layout::kDragReserve == 96);
}

TEST_CASE("viewportBounds parity: sidebar and bookmarks combinations", "[layout][viewport]") {
  SECTION("sidebar open, bookmarks hidden (1600x900)") {
    ExpectRect(ViewportBounds({1600, 900, true, false}), 168, 32, 1424, 860);
  }

  SECTION("sidebar closed, bookmarks hidden (1600x900)") {
    ExpectRect(ViewportBounds({1600, 900, false, false}), 8, 32, 1584, 860);
  }

  SECTION("sidebar open, bookmarks visible (1280x800)") {
    ExpectRect(ViewportBounds({1280, 800, true, true}), 168, 62, 1104, 730);
  }

  SECTION("sidebar closed, bookmarks visible (1280x800)") {
    ExpectRect(ViewportBounds({1280, 800, false, true}), 8, 62, 1264, 730);
  }
}

TEST_CASE("viewportBounds parity: degenerate geometry", "[layout][viewport][edge]") {
  SECTION("window smaller than the chrome clamps origin, never goes negative") {
    // wantedX=168 and wantedY=62 both exceed the window, so x/y clamp to the
    // outer size and the viewport collapses to zero rather than inverting.
    ExpectRect(ViewportBounds({100, 20, true, true}), 100, 20, 0, 0);
  }

  SECTION("zero-size window") {
    ExpectRect(ViewportBounds({0, 0, true, false}), 0, 0, 0, 0);
  }

  SECTION("negative width coerces to zero") {
    ExpectRect(ViewportBounds({-500, 900, true, false}), 0, 32, 0, 860);
  }
}

TEST_CASE("viewportBounds parity: numeric coercion and rounding", "[layout][viewport][edge]") {
  SECTION("fractional dimensions round the JS way") {
    // 1600.5 -> 1601 (ties toward +Infinity), 899.4 -> 899.
    ExpectRect(ViewportBounds({1600.5, 899.4, true, false}), 168, 32, 1425, 859);
  }

  SECTION("non-numeric input collapses to zero") {
    ViewportOptions options;
    options.width = std::nan("");
    options.height = std::nan("");
    ExpectRect(ViewportBounds(options), 0, 0, 0, 0);
  }

  SECTION("defaults match the JS destructuring defaults") {
    // JS: sidebarOpen = true, bookmarksVisible = false.
    ViewportOptions defaults;
    defaults.width = 1600;
    defaults.height = 900;
    CHECK(ViewportBounds(defaults) == ViewportBounds({1600, 900, true, false}));
  }
}

TEST_CASE("dynamicTabMax parity: shrink-to-fit behaviour", "[layout][tabs]") {
  SECTION("no tabs returns the unconstrained maximum") {
    TabMaxOptions options;
    options.available_width = 1400;
    options.count = 0;
    CHECK(DynamicTabMax(options) == 190);
  }

  SECTION("few tabs in a wide strip clamp to the maximum") {
    TabMaxOptions options;
    options.available_width = 1400;
    options.count = 3;
    CHECK(DynamicTabMax(options) == 190);
  }

  SECTION("single tab clamps to the maximum") {
    TabMaxOptions options;
    options.available_width = 1400;
    options.count = 1;
    CHECK(DynamicTabMax(options) == 190);
  }

  SECTION("mid-range crowding actually shrinks") {
    // The case that exercises the arithmetic rather than a clamp:
    // floor((1400 - 34 - 96 - 56) / 8) == 151.
    TabMaxOptions options;
    options.available_width = 1400;
    options.count = 8;
    CHECK(DynamicTabMax(options) == 151);
  }

  SECTION("narrow window clamps to the minimum") {
    TabMaxOptions options;
    options.available_width = 600;
    options.count = 8;
    CHECK(DynamicTabMax(options) == 95);
  }

  SECTION("extreme crowding still clamps to the minimum, never below") {
    TabMaxOptions options;
    options.available_width = 300;
    options.count = 20;
    CHECK(DynamicTabMax(options) == 95);
  }
}

TEST_CASE("dynamicTabMax parity: coercion corners", "[layout][tabs][edge]") {
  SECTION("fractional count floors to the same result as the integer count") {
    TabMaxOptions fractional;
    fractional.available_width = 1400;
    fractional.count = 8.9;
    CHECK(DynamicTabMax(fractional) == 151);
  }

  SECTION("zero available width clamps to the minimum") {
    TabMaxOptions options;
    options.available_width = 0;
    options.count = 5;
    CHECK(DynamicTabMax(options) == 95);
  }

  SECTION("non-numeric count is treated as no tabs") {
    TabMaxOptions options;
    options.available_width = 1400;
    options.count = std::nan("");
    CHECK(DynamicTabMax(options) == 190);
  }
}

// The caption metrics are new to the CEF implementation — they have no JS
// counterpart, so these assert internal consistency rather than parity. They
// matter because the C++ hit-testing and the topbar CSS both position the
// caption buttons, and a disagreement silently breaks the Snap Layouts flyout.
TEST_CASE("Caption buttons tile right-aligned without gaps", "[layout][caption]") {
  using frame::layout::CloseButtonRect;
  using frame::layout::kCaptionButtonWidth;
  using frame::layout::MaximizeButtonRect;
  using frame::layout::MinimizeButtonRect;

  const int width = 1200;
  const auto close = CloseButtonRect(width);
  const auto maximize = MaximizeButtonRect(width);
  const auto minimize = MinimizeButtonRect(width);

  SECTION("close sits flush against the right edge") {
    CHECK(close.x + close.width == width);
  }

  SECTION("buttons are contiguous, right to left") {
    CHECK(maximize.x + maximize.width == close.x);
    CHECK(minimize.x + minimize.width == maximize.x);
  }

  SECTION("each is one caption width, spanning the topbar") {
    CHECK(close.width == kCaptionButtonWidth);
    CHECK(maximize.width == kCaptionButtonWidth);
    CHECK(minimize.width == kCaptionButtonWidth);
    CHECK(close.height == frame::layout::kTopbarHeight);
  }

  SECTION("the block reserves exactly three button widths") {
    CHECK(frame::layout::CaptionButtonsWidth() == kCaptionButtonWidth * 3);
    CHECK(width - minimize.x == frame::layout::CaptionButtonsWidth());
  }
}

TEST_CASE("Caption hit-testing accepts only its own rectangle",
          "[layout][caption]") {
  const int width = 1200;
  const auto maximize = frame::layout::MaximizeButtonRect(width);

  SECTION("inside") {
    CHECK(maximize.Contains(maximize.x + 1, 1));
    CHECK(maximize.Contains(maximize.x + maximize.width - 1,
                            frame::layout::kTopbarHeight - 1));
  }

  SECTION("the far edges belong to the neighbouring buttons") {
    // Half-open rectangles: without this, adjacent buttons would both claim
    // the shared boundary column.
    CHECK_FALSE(maximize.Contains(maximize.x - 1, 1));
    CHECK_FALSE(maximize.Contains(maximize.x + maximize.width, 1));
  }

  SECTION("below the topbar is not the caption") {
    CHECK_FALSE(maximize.Contains(maximize.x + 1,
                                  frame::layout::kTopbarHeight));
  }
}
