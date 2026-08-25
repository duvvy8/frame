// Frame — shared chrome layout constants and pure geometry.
//
// Ported 1:1 from the original Electron build's src/shared/chrome-layout.js.
// This header is the SINGLE SOURCE OF TRUTH for chrome geometry: the browser
// process, every OSR-rendered chrome surface, and the corner-mask drawing code
// must all agree with it. Nothing may re-declare these numbers locally.
//
// The functions below are deliberately framework-free — no CEF, no Win32, no
// platform headers — so they stay unit-testable in isolation and can be
// included from anywhere in the codebase.
//
// Trailing comments give each constant's original JS name so the two codebases
// remain greppable against each other during the migration.

#ifndef FRAME_SHARED_CHROME_LAYOUT_H_
#define FRAME_SHARED_CHROME_LAYOUT_H_

#include <algorithm>
#include <cmath>

namespace frame::layout {

inline constexpr int kTopbarHeight = 32;          // TOPBAR_HEIGHT
inline constexpr int kSidebarWidth = 168;         // SIDEBAR_WIDTH
inline constexpr int kCollapsedRailWidth = 8;     // COLLAPSED_RAIL_WIDTH
inline constexpr int kOuterInset = 0;             // OUTER_INSET

// The window's outer corner is DWM's, not Frame's. Drawing a rounded shell
// inside a square window left the difference between the two curves showing
// the window's own background — the black 90-degree corner. Zero here means the
// shell paints right into the corner and the compositor clips it, so the curve
// matches every other Windows 11 window and cannot come apart from it.
inline constexpr int kOuterRadius = 0;            // OUTER_RADIUS

inline constexpr int kShellInset = 8;             // SHELL_INSET
inline constexpr int kViewportRadius = 12;        // VIEWPORT_RADIUS
inline constexpr int kBookmarksHeight = 30;       // BOOKMARKS_HEIGHT
inline constexpr int kSidebarTransitionMs = 210;  // SIDEBAR_TRANSITION_MS

inline constexpr int kTabMinWidth = 95;           // TAB_MIN_WIDTH
inline constexpr int kTabMaxWidth = 190;          // TAB_MAX_WIDTH
inline constexpr int kTabGap = 8;                 // TAB_GAP
inline constexpr int kNewTabWidth = 34;           // NEW_TAB_WIDTH
inline constexpr int kDragReserve = 96;           // DRAG_RESERVE

namespace internal {

// Mirrors the JS `Number(v) || 0` coercion at each call site: a value that is
// not a usable number becomes 0.
//
// Faithful for NaN (JS: `Number(NaN) || 0 === 0`). DELIBERATE DEVIATION for
// +/-Infinity, which JS would propagate but which would make the narrowing cast
// to int undefined behaviour here. Real window geometry is never infinite, so
// collapsing it to 0 trades an unreachable case for a defined one.
inline double CoerceNumber(double value) {
  return std::isfinite(value) ? value : 0.0;
}

// JS `Math.round` breaks ties toward +Infinity (Math.round(-2.5) === -2),
// whereas C++ std::round breaks ties away from zero (std::round(-2.5) == -3).
// Frame's callers clamp negatives to 0 immediately afterward, so the two agree
// in practice — but the port matches the original exactly rather than relying
// on that.
inline double JsRound(double value) {
  return std::floor(value + 0.5);
}

// JS `clamp(value, minimum, maximum)`: Math.min(maximum, Math.max(minimum, v)).
// Note the asymmetry — if the bounds are inverted, `maximum` wins. Preserved
// as-is rather than "fixed", so behaviour cannot drift from the original.
inline double JsClamp(double value, double minimum, double maximum) {
  return std::min(maximum, std::max(minimum, value));
}

}  // namespace internal

// Inputs to ViewportBounds(). Defaults match the JS destructuring defaults
// (`sidebarOpen = true, bookmarksVisible = false`).
struct ViewportOptions {
  double width = 0.0;
  double height = 0.0;
  bool sidebar_open = true;
  bool bookmarks_visible = false;
};

// The one native-page rectangle used by tabs, overlays, and visual QA.
struct ViewportRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  int radius = kViewportRadius;
};

inline bool operator==(const ViewportRect& lhs, const ViewportRect& rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.width == rhs.width &&
         lhs.height == rhs.height && lhs.radius == rhs.radius;
}

inline bool operator!=(const ViewportRect& lhs, const ViewportRect& rhs) {
  return !(lhs == rhs);
}

/** The one native-page rectangle used by tabs, overlays, and visual QA. */
inline ViewportRect ViewportBounds(const ViewportOptions& options = {}) {
  const double outer_width =
      std::max(0.0, internal::JsRound(internal::CoerceNumber(options.width)));
  const double outer_height =
      std::max(0.0, internal::JsRound(internal::CoerceNumber(options.height)));

  const double wanted_x = options.sidebar_open
                              ? static_cast<double>(kOuterInset + kSidebarWidth)
                              : static_cast<double>(kCollapsedRailWidth);
  const double wanted_y =
      static_cast<double>(kTopbarHeight + (options.bookmarks_visible ? kBookmarksHeight : 0));

  // Clamped against the outer size so a window smaller than the chrome itself
  // yields an on-screen origin and a zero-size viewport, never a negative one.
  const double x = std::min(outer_width, wanted_x);
  const double y = std::min(outer_height, wanted_y);

  ViewportRect rect;
  rect.x = static_cast<int>(x);
  rect.y = static_cast<int>(y);
  rect.width = static_cast<int>(std::max(0.0, outer_width - x - kOuterInset - kShellInset));
  rect.height = static_cast<int>(std::max(0.0, outer_height - y - kOuterInset - kShellInset));
  rect.radius = kViewportRadius;
  return rect;
}

// Inputs to DynamicTabMax(). Defaults match the JS destructuring defaults.
struct TabMaxOptions {
  double available_width = 0.0;
  double count = 0.0;
  int gap = kTabGap;
  int plus_width = kNewTabWidth;
  int drag_reserve = kDragReserve;
  int minimum = kTabMinWidth;
  int maximum = kTabMaxWidth;
};

/** Cap long tabs without forcing short labels to consume the same width. */
inline int DynamicTabMax(const TabMaxOptions& options = {}) {
  const double tab_count =
      std::max(0.0, std::floor(internal::CoerceNumber(options.count)));

  // JS `if (!tabCount) return maximum` — returns before any arithmetic, so a
  // zero-tab strip reports the unconstrained maximum rather than a division
  // by zero.
  if (tab_count == 0.0) {
    return options.maximum;
  }

  // Note: unlike ViewportBounds(), the original does NOT round the width here.
  const double width = std::max(0.0, internal::CoerceNumber(options.available_width));
  const double gaps = std::max(0.0, tab_count - 1.0) * options.gap;
  const double share = std::floor(
      (width - options.plus_width - options.drag_reserve - gaps) / tab_count);

  return static_cast<int>(internal::JsClamp(share, options.minimum, options.maximum));
}

}  // namespace frame::layout

#endif  // FRAME_SHARED_CHROME_LAYOUT_H_
