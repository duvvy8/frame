#include "browser/main_window.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <cstring>
#include <string>

#include "include/cef_app.h"
#include "shared/chrome_layout.h"

// Present in the Windows 11 SDK, defined defensively so the build does not
// depend on which SDK version happens to be installed.
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace frame {
namespace {

const wchar_t kWindowClass[] = L"FrameMainWindow";
const wchar_t kWindowTitle[] = L"Frame";

// Shell colours. The surfaces paint their own glass on top; these only decide
// what shows through where nothing has been composited yet.
const COLORREF kShellBackground = RGB(0x0b, 0x0b, 0x0d);
const COLORREF kViewportPlaceholder = RGB(0x00, 0x00, 0x00);

int MouseModifiers(WPARAM wparam) {
  int modifiers = 0;
  if (wparam & MK_CONTROL) {
    modifiers |= EVENTFLAG_CONTROL_DOWN;
  }
  if (wparam & MK_SHIFT) {
    modifiers |= EVENTFLAG_SHIFT_DOWN;
  }
  if (wparam & MK_LBUTTON) {
    modifiers |= EVENTFLAG_LEFT_MOUSE_BUTTON;
  }
  if (wparam & MK_RBUTTON) {
    modifiers |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
  }
  if (::GetKeyState(VK_MENU) < 0) {
    modifiers |= EVENTFLAG_ALT_DOWN;
  }
  return modifiers;
}

}  // namespace

MainWindow::MainWindow(const Options& options) : options_(options) {}

MainWindow::~MainWindow() = default;

bool MainWindow::Create(HINSTANCE instance) {
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = &MainWindow::WndProc;
  wc.hInstance = instance;
  wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = ::CreateSolidBrush(kShellBackground);
  wc.lpszClassName = kWindowClass;
  ::RegisterClassExW(&wc);

  // WS_OVERLAPPEDWINDOW is kept even though no caption is drawn. The style is
  // what gives the window snapping, the minimise/restore animations, and a
  // taskbar entry that behaves; WM_NCCALCSIZE removes the visible frame
  // without giving those up.
  hwnd_ = ::CreateWindowExW(0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW,
                            options_.x, options_.y, options_.width,
                            options_.height, nullptr, nullptr, instance, this);
  if (!hwnd_) {
    return false;
  }

  ApplyRoundedCorners();

  // Forces a WM_NCCALCSIZE pass so the frameless layout takes effect before
  // the window is shown, rather than visibly reflowing after first paint.
  ::SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOACTIVATE);

  RECT client = {};
  ::GetClientRect(hwnd_, &client);
  client_width_ = client.right - client.left;
  client_height_ = client.bottom - client.top;

  ::ShowWindow(hwnd_, options_.no_activate ? SW_SHOWNOACTIVATE : SW_SHOW);
  ::UpdateWindow(hwnd_);
  return true;
}

void MainWindow::ApplyRoundedCorners() {
  // The outer corner belongs to DWM, never to us. Asking the compositor for it
  // means the curve matches every other Windows 11 window and cannot come
  // apart from the window's own background at the corner.
  UINT preference = DWMWCP_ROUND;
  ::DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &preference,
                          sizeof(preference));

  // Keeps the thin DWM border dark instead of light-themed.
  BOOL dark = TRUE;
  ::DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark,
                          sizeof(dark));
}

CefRect MainWindow::SurfaceBounds(SurfaceId id) const {
  switch (id) {
    case SurfaceId::kTopbar:
      // Full width, across the very top — now including the pixels the system
      // caption used to occupy.
      return CefRect(0, 0, client_width_, layout::kTopbarHeight);

    case SurfaceId::kSidebar: {
      const int width =
          sidebar_open_ ? layout::kSidebarWidth : layout::kCollapsedRailWidth;
      const int height = client_height_ - layout::kTopbarHeight;
      return CefRect(0, layout::kTopbarHeight, width, height > 0 ? height : 0);
    }

    default:
      return CefRect(0, 0, 0, 0);
  }
}

void MainWindow::SetSurfaceBrowser(SurfaceId id,
                                   CefRefPtr<CefBrowser> browser) {
  layer(id).browser = browser;
  if (id == SurfaceId::kTopbar && browser) {
    PushWindowState();
  }
}

void MainWindow::SetDragExclusions(std::vector<layout::IntRect> regions) {
  drag_exclusions_ = std::move(regions);
}

// --- window commands ------------------------------------------------------

bool MainWindow::IsWindowMaximized() const {
  return hwnd_ && ::IsZoomed(hwnd_) != 0;
}

void MainWindow::Minimize() {
  ::ShowWindow(hwnd_, SW_MINIMIZE);
}

void MainWindow::ToggleMaximize() {
  ::ShowWindow(hwnd_, IsWindowMaximized() ? SW_RESTORE : SW_MAXIMIZE);
}

void MainWindow::CloseWindow() {
  ::PostMessage(hwnd_, WM_CLOSE, 0, 0);
}

void MainWindow::PushWindowState() {
  Layer& top = layer(SurfaceId::kTopbar);
  if (!top.browser) {
    return;
  }
  CefRefPtr<CefFrame> main_frame = top.browser->GetMainFrame();
  if (!main_frame) {
    return;
  }
  // The caption button has to show restore-vs-maximise, and only the window
  // knows which it is.
  const std::string js =
      std::string("window.FrameShell && FrameShell.onWindowState({maximized:") +
      (IsWindowMaximized() ? "true" : "false") + "});";
  main_frame->ExecuteJavaScript(js, main_frame->GetURL(), 0);
}

// --- frameless plumbing ---------------------------------------------------

LRESULT MainWindow::HandleNcCalcSize(WPARAM wparam, LPARAM lparam) {
  if (!wparam) {
    return ::DefWindowProc(hwnd_, WM_NCCALCSIZE, wparam, lparam);
  }

  auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);

  if (IsWindowMaximized()) {
    // A maximised window's rect deliberately overhangs the monitor by the
    // frame thickness. Without putting that inset back, the topbar would sit
    // off-screen and the caption buttons would be unreachable.
    const int border = ::GetSystemMetrics(SM_CXSIZEFRAME) +
                       ::GetSystemMetrics(SM_CXPADDEDBORDER);
    params->rgrc[0].left += border;
    params->rgrc[0].right -= border;
    params->rgrc[0].top += border;
    params->rgrc[0].bottom -= border;
  }

  // Returning 0 with the rect otherwise untouched makes the client area the
  // whole window: no caption, no frame. The resize borders become ours to
  // hit-test, which HitTest() does.
  return 0;
}

LRESULT MainWindow::HitTest(POINT screen_point) {
  POINT p = screen_point;
  ::ScreenToClient(hwnd_, &p);

  const int border = layout::kResizeBorderThickness;

  // Resize grips. A maximised window has none.
  if (!IsWindowMaximized()) {
    const bool top = p.y >= 0 && p.y < border;
    const bool bottom = p.y < client_height_ && p.y >= client_height_ - border;
    const bool left = p.x >= 0 && p.x < border;
    const bool right = p.x < client_width_ && p.x >= client_width_ - border;

    if (top && left) {
      return HTTOPLEFT;
    }
    if (top && right) {
      return HTTOPRIGHT;
    }
    if (bottom && left) {
      return HTBOTTOMLEFT;
    }
    if (bottom && right) {
      return HTBOTTOMRIGHT;
    }
    if (top) {
      return HTTOP;
    }
    if (bottom) {
      return HTBOTTOM;
    }
    if (left) {
      return HTLEFT;
    }
    if (right) {
      return HTRIGHT;
    }
  }

  if (p.y >= 0 && p.y < layout::kTopbarHeight) {
    // Reporting HTMAXBUTTON is the whole reason the Snap Layouts flyout
    // appears on hover. Windows offers it for that hit-test result and
    // nothing else, so this cannot be handled as an ordinary client click.
    if (layout::MaximizeButtonRect(client_width_).Contains(p.x, p.y)) {
      return HTMAXBUTTON;
    }
    // Minimise and close are plain client clicks, handled by the surface.
    if (layout::MinimizeButtonRect(client_width_).Contains(p.x, p.y) ||
        layout::CloseButtonRect(client_width_).Contains(p.x, p.y)) {
      return HTCLIENT;
    }
    // Anything the surface flagged as interactive stays clickable; the rest of
    // the topbar drags the window.
    for (const layout::IntRect& region : drag_exclusions_) {
      if (region.Contains(p.x, p.y)) {
        return HTCLIENT;
      }
    }
    return HTCAPTION;
  }

  return HTCLIENT;
}

// --- painting -------------------------------------------------------------

void MainWindow::PaintLayer(HDC hdc, SurfaceId id) {
  const Layer& source = layer(id);
  if (source.pixels.empty()) {
    return;
  }
  const CefRect bounds = SurfaceBounds(id);

  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = source.width;
  // Negative height: OnPaint delivers top-down, GDI assumes bottom-up. Without
  // this the surface renders upside down.
  info.bmiHeader.biHeight = -source.height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;

  ::SetDIBitsToDevice(hdc, bounds.x, bounds.y, source.width, source.height, 0,
                      0, 0, source.height, source.pixels.data(), &info,
                      DIB_RGB_COLORS);
}

void MainWindow::Paint(HDC hdc) {
  // Clear first. WM_ERASEBKGND is suppressed to avoid flicker, so nothing else
  // clears the shell margins — the inset gutters and any region a surface does
  // not cover would otherwise keep whatever pixels were there before.
  RECT client = {0, 0, client_width_, client_height_};
  HBRUSH shell = ::CreateSolidBrush(kShellBackground);
  ::FillRect(hdc, &client, shell);
  ::DeleteObject(shell);

  // The page viewport, straight from the shared layout math. Rounded with
  // VIEWPORT_RADIUS so the inner content corner matches the design even before
  // a real page browser occupies it.
  const layout::ViewportRect viewport = layout::ViewportBounds(
      {static_cast<double>(client_width_), static_cast<double>(client_height_),
       sidebar_open_, /*bookmarks_visible=*/false});

  if (viewport.width > 0 && viewport.height > 0) {
    HRGN rounded = ::CreateRoundRectRgn(
        viewport.x, viewport.y, viewport.x + viewport.width + 1,
        viewport.y + viewport.height + 1, viewport.radius * 2,
        viewport.radius * 2);
    HBRUSH fill = ::CreateSolidBrush(kViewportPlaceholder);
    ::FillRgn(hdc, rounded, fill);
    ::DeleteObject(fill);
    ::DeleteObject(rounded);
  }

  PaintLayer(hdc, SurfaceId::kTopbar);
  PaintLayer(hdc, SurfaceId::kSidebar);
}

void MainWindow::OnSurfacePaint(SurfaceId id,
                                const void* buffer,
                                int width,
                                int height) {
  Layer& target = layer(id);
  const size_t bytes = static_cast<size_t>(width) * height * 4;
  if (target.pixels.size() != bytes) {
    target.pixels.resize(bytes);
  }
  memcpy(target.pixels.data(), buffer, bytes);
  target.width = width;
  target.height = height;

  if (hwnd_) {
    const CefRect bounds = SurfaceBounds(id);
    RECT dirty = {bounds.x, bounds.y, bounds.x + width, bounds.y + height};
    ::InvalidateRect(hwnd_, &dirty, FALSE);
  }
}

void MainWindow::NotifySurfacesResized() {
  for (size_t i = 0; i < static_cast<size_t>(SurfaceId::kCount); ++i) {
    Layer& target = layers_[i];
    if (target.browser) {
      // Makes CEF re-query GetViewRect and repaint at the new size.
      target.browser->GetHost()->WasResized();
    }
  }
}

// --- input routing --------------------------------------------------------

bool MainWindow::SurfaceAt(int x,
                           int y,
                           SurfaceId* id,
                           int* local_x,
                           int* local_y) const {
  // Topbar first: it spans the full width and wins along the top edge.
  const SurfaceId order[] = {SurfaceId::kTopbar, SurfaceId::kSidebar};
  for (SurfaceId candidate : order) {
    const CefRect bounds = SurfaceBounds(candidate);
    if (x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y &&
        y < bounds.y + bounds.height) {
      *id = candidate;
      *local_x = x - bounds.x;
      *local_y = y - bounds.y;
      return true;
    }
  }
  return false;
}

void MainWindow::SendMouseLeaveToAll() {
  for (size_t i = 0; i < static_cast<size_t>(SurfaceId::kCount); ++i) {
    if (layers_[i].browser) {
      CefMouseEvent event;
      layers_[i].browser->GetHost()->SendMouseMoveEvent(event,
                                                        /*mouseLeave=*/true);
    }
  }
}

void MainWindow::ForwardMouseMove(int x, int y, WPARAM wparam) {
  SurfaceId id = SurfaceId::kTopbar;
  int local_x = 0;
  int local_y = 0;
  const bool inside = SurfaceAt(x, y, &id, &local_x, &local_y);

  // Surfaces the cursor has left need an explicit leave, or their hover states
  // latch on and never clear.
  for (size_t i = 0; i < static_cast<size_t>(SurfaceId::kCount); ++i) {
    Layer& target = layers_[i];
    if (!target.browser) {
      continue;
    }
    const bool is_current = inside && static_cast<size_t>(id) == i;
    CefMouseEvent event;
    event.x = is_current ? local_x : 0;
    event.y = is_current ? local_y : 0;
    event.modifiers = MouseModifiers(wparam);
    target.browser->GetHost()->SendMouseMoveEvent(event, !is_current);
  }
}

void MainWindow::ForwardMouseButton(int x,
                                    int y,
                                    WPARAM wparam,
                                    CefBrowserHost::MouseButtonType button,
                                    bool up) {
  SurfaceId id = SurfaceId::kTopbar;
  int local_x = 0;
  int local_y = 0;
  if (!SurfaceAt(x, y, &id, &local_x, &local_y) || !layer(id).browser) {
    return;
  }
  CefMouseEvent event;
  event.x = local_x;
  event.y = local_y;
  event.modifiers = MouseModifiers(wparam);
  layer(id).browser->GetHost()->SendMouseClickEvent(event, button, up,
                                                    /*clickCount=*/1);
}

void MainWindow::ForwardMouseWheel(int screen_x,
                                   int screen_y,
                                   int delta,
                                   WPARAM wparam) {
  // Wheel messages carry screen coordinates, unlike the other mouse messages.
  POINT point = {screen_x, screen_y};
  ::ScreenToClient(hwnd_, &point);

  SurfaceId id = SurfaceId::kTopbar;
  int local_x = 0;
  int local_y = 0;
  if (!SurfaceAt(point.x, point.y, &id, &local_x, &local_y) ||
      !layer(id).browser) {
    return;
  }
  CefMouseEvent event;
  event.x = local_x;
  event.y = local_y;
  event.modifiers = MouseModifiers(wparam);
  layer(id).browser->GetHost()->SendMouseWheelEvent(event, /*deltaX=*/0, delta);
}

// static
LRESULT CALLBACK MainWindow::WndProc(HWND hwnd,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam) {
  MainWindow* self = nullptr;
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = static_cast<MainWindow*>(create->lpCreateParams);
    ::SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    self->hwnd_ = hwnd;
  } else {
    self =
        reinterpret_cast<MainWindow*>(::GetWindowLongPtr(hwnd, GWLP_USERDATA));
  }
  if (self) {
    return self->HandleMessage(hwnd, message, wparam, lparam);
  }
  return ::DefWindowProc(hwnd, message, wparam, lparam);
}

LRESULT MainWindow::HandleMessage(HWND hwnd,
                                  UINT message,
                                  WPARAM wparam,
                                  LPARAM lparam) {
  switch (message) {
    case WM_NCCALCSIZE:
      if (options_.system_titlebar) {
        break;
      }
      return HandleNcCalcSize(wparam, lparam);

    case WM_NCHITTEST: {
      if (options_.system_titlebar) {
        break;
      }
      POINT screen = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      return HitTest(screen);
    }

    // The caption buttons sit in the non-client area as far as Windows is
    // concerned, so ordinary mouse messages never reach them. These four keep
    // the maximise button hovering and clicking like a real control while
    // still reporting HTMAXBUTTON for Snap Layouts.
    case WM_NCMOUSEMOVE: {
      if (wparam == HTMAXBUTTON) {
        if (!tracking_nc_mouse_) {
          TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE | TME_NONCLIENT,
                                   hwnd, 0};
          ::TrackMouseEvent(&track);
          tracking_nc_mouse_ = true;
        }
        POINT p = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ::ScreenToClient(hwnd, &p);
        Layer& top = layer(SurfaceId::kTopbar);
        if (top.browser) {
          CefMouseEvent event;
          event.x = p.x;
          event.y = p.y;
          top.browser->GetHost()->SendMouseMoveEvent(event,
                                                     /*mouseLeave=*/false);
        }
        return 0;
      }
      break;
    }

    case WM_NCMOUSELEAVE:
      tracking_nc_mouse_ = false;
      SendMouseLeaveToAll();
      break;

    case WM_NCLBUTTONDOWN:
      if (wparam == HTMAXBUTTON) {
        // Swallowed so the system does not start its own caption drag.
        return 0;
      }
      break;

    case WM_NCLBUTTONUP:
      if (wparam == HTMAXBUTTON) {
        ToggleMaximize();
        return 0;
      }
      break;

    case WM_ERASEBKGND:
      // Everything is painted in WM_PAINT; erasing here only causes flicker.
      return 1;

    case WM_PAINT: {
      PAINTSTRUCT ps = {};
      HDC hdc = ::BeginPaint(hwnd, &ps);
      Paint(hdc);
      ::EndPaint(hwnd, &ps);
      return 0;
    }

    case WM_SIZE:
      client_width_ = LOWORD(lparam);
      client_height_ = HIWORD(lparam);
      NotifySurfacesResized();
      PushWindowState();
      return 0;

    case WM_MOUSEMOVE: {
      if (!tracking_mouse_) {
        // Without this the window never receives WM_MOUSELEAVE, and hover
        // states stay stuck when the cursor exits the window entirely.
        TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd, 0};
        ::TrackMouseEvent(&track);
        tracking_mouse_ = true;
      }
      ForwardMouseMove(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), wparam);
      return 0;
    }

    case WM_MOUSELEAVE:
      tracking_mouse_ = false;
      SendMouseLeaveToAll();
      return 0;

    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
      ForwardMouseButton(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), wparam,
                         MBT_LEFT, message == WM_LBUTTONUP);
      return 0;

    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
      ForwardMouseButton(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), wparam,
                         MBT_RIGHT, message == WM_RBUTTONUP);
      return 0;

    case WM_MOUSEWHEEL:
      ForwardMouseWheel(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam),
                        GET_WHEEL_DELTA_WPARAM(wparam),
                        GET_KEYSTATE_WPARAM(wparam));
      return 0;

    case WM_CLOSE:
      for (size_t i = 0; i < static_cast<size_t>(SurfaceId::kCount); ++i) {
        if (layers_[i].browser) {
          layers_[i].browser->GetHost()->CloseBrowser(/*force_close=*/true);
        }
      }
      break;

    case WM_DESTROY:
      CefQuitMessageLoop();
      return 0;

    default:
      break;
  }
  return ::DefWindowProc(hwnd, message, wparam, lparam);
}

}  // namespace frame
