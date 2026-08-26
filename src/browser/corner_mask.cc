#include "browser/corner_mask.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace frame {
namespace {

const wchar_t kMaskClass[] = L"FrameCornerMask";

LRESULT CALLBACK MaskWndProc(HWND hwnd,
                             UINT message,
                             WPARAM wparam,
                             LPARAM lparam) {
  // Layered windows are painted entirely through UpdateLayeredWindow, so there
  // is nothing to do here.
  return ::DefWindowProc(hwnd, message, wparam, lparam);
}

void EnsureClass(HINSTANCE instance) {
  static bool registered = false;
  if (registered) {
    return;
  }
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = &MaskWndProc;
  wc.hInstance = instance;
  wc.lpszClassName = kMaskClass;
  ::RegisterClassExW(&wc);
  registered = true;
}

}  // namespace

CornerMask::CornerMask() = default;

CornerMask::~CornerMask() {
  for (HWND& window : windows_) {
    if (window) {
      ::DestroyWindow(window);
      window = nullptr;
    }
  }
}

bool CornerMask::Create(HWND parent, HINSTANCE instance) {
  EnsureClass(instance);
  owner_ = parent;
  for (int i = 0; i < kCount; ++i) {
    // Owned POPUPs, not child windows.
    //
    // The page window carries WS_EX_NOREDIRECTIONBITMAP: Chromium composites
    // it directly through DWM with no redirection surface, so a sibling child
    // window can never draw over it — UpdateLayeredWindow reports success and
    // nothing appears. An owned top-level window is composited above its owner
    // by DWM itself, which is the only layer that sits above the page.
    //
    // TOOLWINDOW keeps them out of Alt-Tab, TRANSPARENT lets clicks through to
    // the page, NOACTIVATE stops them ever taking focus.
    windows_[i] = ::CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        kMaskClass, L"", WS_POPUP, 0, 0, 1, 1, parent, nullptr, instance,
        nullptr);
    if (!windows_[i]) {
      return false;
    }
  }
  return true;
}

void CornerMask::Paint(Corner corner, int radius, COLORREF shell_color) {
  HWND window = windows_[corner];
  if (!window || radius <= 0) {
    return;
  }

  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = radius;
  info.bmiHeader.biHeight = -radius;  // Top-down.
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;

  void* bits = nullptr;
  HDC screen = ::GetDC(nullptr);
  HBITMAP bitmap =
      ::CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!bitmap) {
    ::ReleaseDC(nullptr, screen);
    return;
  }

  const double r = static_cast<double>(radius);
  // Centre of the corner's circle, in this bitmap's coordinates. The bitmap
  // always covers the radius-sized square at the very corner of the viewport.
  const double cx = (corner == kTopLeft || corner == kBottomLeft) ? r : 0.0;
  const double cy = (corner == kTopLeft || corner == kTopRight) ? r : 0.0;

  const int blue = GetBValue(shell_color);
  const int green = GetGValue(shell_color);
  const int red = GetRValue(shell_color);

  auto* pixels = static_cast<uint8_t*>(bits);
  for (int y = 0; y < radius; ++y) {
    for (int x = 0; x < radius; ++x) {
      const double dx = (x + 0.5) - cx;
      const double dy = (y + 0.5) - cy;
      const double distance = std::sqrt(dx * dx + dy * dy);

      // Everything outside the circle is shell; the half-pixel band across the
      // boundary is what antialiases the curve.
      double coverage = distance - r + 0.5;
      coverage = std::max(0.0, std::min(1.0, coverage));

      const uint8_t alpha = static_cast<uint8_t>(coverage * 255.0 + 0.5);
      uint8_t* pixel = pixels + (static_cast<size_t>(y) * radius + x) * 4;
      // Premultiplied alpha, which is what ULW_ALPHA expects.
      pixel[0] = static_cast<uint8_t>(blue * coverage + 0.5);
      pixel[1] = static_cast<uint8_t>(green * coverage + 0.5);
      pixel[2] = static_cast<uint8_t>(red * coverage + 0.5);
      pixel[3] = alpha;
    }
  }

  HDC memory = ::CreateCompatibleDC(screen);
  HGDIOBJ previous = ::SelectObject(memory, bitmap);

  SIZE size = {radius, radius};
  POINT source = {0, 0};
  BLENDFUNCTION blend = {};
  blend.BlendOp = AC_SRC_OVER;
  blend.SourceConstantAlpha = 255;
  blend.AlphaFormat = AC_SRC_ALPHA;

  ::UpdateLayeredWindow(window, screen, nullptr, &size, memory, &source, 0,
                        &blend, ULW_ALPHA);

  ::SelectObject(memory, previous);
  ::DeleteDC(memory);
  ::DeleteObject(bitmap);
  ::ReleaseDC(nullptr, screen);
}

void CornerMask::Layout(const layout::ViewportRect& viewport,
                        const COLORREF (&corner_colors)[kCornerCount]) {
  const int radius = viewport.radius;
  if (viewport.width <= 0 || viewport.height <= 0 || radius <= 0) {
    Hide();
    return;
  }

  // Repainting is only needed when a mask's own appearance changes; moving it
  // is just a SetWindowPos. Tracked per corner, so a gradient shifting under
  // one of them does not cost a repaint of the other three.
  const bool radius_changed = radius != painted_radius_;
  for (int i = 0; i < kCount; ++i) {
    // CLR_INVALID means "this corner does not need faking".
    //
    // A viewport corner that coincides with a corner of the WINDOW is already
    // rounded by DWM, and covering it with our own wedge would only paint a
    // notch over a curve that is being drawn correctly without us.
    if (corner_colors[i] == CLR_INVALID) {
      continue;
    }
    if (!radius_changed && corner_colors[i] == painted_colors_[i]) {
      continue;
    }
    Paint(static_cast<Corner>(i), radius, corner_colors[i]);
    painted_colors_[i] = corner_colors[i];
  }
  painted_radius_ = radius;

  const int left = viewport.x;
  const int top = viewport.y;
  const int right = viewport.x + viewport.width - radius;
  const int bottom = viewport.y + viewport.height - radius;

  POINT positions[kCount] = {
      {left, top}, {right, top}, {left, bottom}, {right, bottom}};

  for (int i = 0; i < kCount; ++i) {
    if (!windows_[i]) {
      continue;
    }
    if (corner_colors[i] == CLR_INVALID) {
      ::ShowWindow(windows_[i], SW_HIDE);
      continue;
    }
    // These are top-level windows, so client coordinates have to be converted.
    // They therefore need repositioning when the window MOVES, not only when it
    // resizes.
    POINT screen = positions[i];
    ::ClientToScreen(owner_, &screen);
    ::SetWindowPos(windows_[i], HWND_TOP, screen.x, screen.y, radius, radius,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
  }
}

void CornerMask::RaiseAbove(HWND page) {
  if (!page) {
    return;
  }
  for (HWND window : windows_) {
    if (window) {
      // Above the page, without moving, resizing or activating anything.
      ::SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
  }
}

void CornerMask::Hide() {
  for (HWND window : windows_) {
    if (window) {
      ::ShowWindow(window, SW_HIDE);
    }
  }
}

}  // namespace frame

