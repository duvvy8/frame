#ifndef FRAME_BROWSER_CORNER_MASK_H_
#define FRAME_BROWSER_CORNER_MASK_H_

#include <windows.h>

#include "shared/chrome_layout.h"

namespace frame {

// The four corner masks that fake VIEWPORT_RADIUS on the page.
//
// The page is a real child browser window and paints its own square corners.
// Clipping it with SetWindowRgn does round it, but an HRGN is a binary mask
// with no antialiasing, so the curve comes out visibly stair-stepped — and the
// shell showing through the hard-edged notch reads as something bleeding
// through rather than as a rounded corner.
//
// So the corners are covered instead of clipped, which is the same approach the
// Electron build took: four small surfaces sitting ON TOP of the page, each
// painting the shell colour with an antialiased wedge cut out of it. Here they
// are layered child windows with per-pixel alpha rather than HTML surfaces,
// because the chrome surfaces are composited in the parent's WM_PAINT and the
// parent paints BENEATH its child windows — anything drawn there would end up
// behind the page instead of over it.
//
// Coverage is computed analytically from the distance to the corner circle,
// which gives a clean edge without pulling in a drawing library.
class CornerMask {
 public:
  CornerMask();
  ~CornerMask();

  CornerMask(const CornerMask&) = delete;
  CornerMask& operator=(const CornerMask&) = delete;

  bool Create(HWND parent, HINSTANCE instance);

  // Positions and repaints all four masks around the given viewport. Safe to
  // call on every layout pass; the bitmaps are only rebuilt when the radius or
  // colour actually changes.
  void Layout(const layout::ViewportRect& viewport, COLORREF shell_color);

  void Hide();

  // Raises the masks above the page, which has to happen after the page window
  // is positioned or the page covers them.
  void RaiseAbove(HWND page);

 private:
  enum Corner { kTopLeft = 0, kTopRight, kBottomLeft, kBottomRight, kCount };

  void Paint(Corner corner, int radius, COLORREF shell_color);

  HWND owner_ = nullptr;
  HWND windows_[kCount] = {};
  int painted_radius_ = -1;
  COLORREF painted_color_ = CLR_INVALID;
};

}  // namespace frame

#endif  // FRAME_BROWSER_CORNER_MASK_H_
