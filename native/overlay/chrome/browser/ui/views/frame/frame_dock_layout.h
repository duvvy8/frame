// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_FRAME_DOCK_LAYOUT_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_FRAME_DOCK_LAYOUT_H_

#include <algorithm>

namespace frame {
inline constexpr int kDockWidth = 48;
inline constexpr int kDockInset = 12;
inline constexpr int kDockGutter = kDockWidth + 2 * kDockInset;
inline constexpr int kDockTabHeight = 36;
inline constexpr int kDockFooterHeight = 48;

// These calculations use DIPs. The gutter belongs to browser chrome, so even
// scrolled/zoomed pages and split panes cannot put controls underneath the
// dock.
struct DockBounds {
  int y;
  int height;
};

inline int DockPreferredHeight(int tab_count) {
  return std::min(600, std::clamp(tab_count, 0, 1000) * kDockTabHeight +
                           kDockFooterHeight + 4);
}

inline DockBounds CalculateDockBounds(int top, int bottom, int preferred) {
  const int available = std::max(0, bottom - top);
  const int height = std::clamp(preferred, 0, std::min(600, available));
  return {top + (available - height) / 2, height};
}

struct ToolbarBounds {
  int x;
  int width;
};

// Center on the window where possible, but respect the real caption-button
// exclusions. At small widths, surrender whitespace before shrinking controls.
inline ToolbarBounds CalculateToolbarBounds(int left,
                                            int right,
                                            int window_center) {
  const int width = std::min(720, std::max(0, right - left));
  return {std::clamp(window_center - width / 2, left,
                     std::max(left, right - width)),
          width};
}
}  // namespace frame

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_FRAME_DOCK_LAYOUT_H_
