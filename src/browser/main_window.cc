#include "browser/main_window.h"

#include <windowsx.h>

#include <cstring>

#include "include/cef_app.h"
#include "shared/chrome_layout.h"

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

  hwnd_ = ::CreateWindowExW(0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW,
                            options_.x, options_.y, options_.width,
                            options_.height, nullptr, nullptr, instance, this);
  if (!hwnd_) {
    return false;
  }

  RECT client = {};
  ::GetClientRect(hwnd_, &client);
  client_width_ = client.right - client.left;
  client_height_ = client.bottom - client.top;

  ::ShowWindow(hwnd_, options_.no_activate ? SW_SHOWNOACTIVATE : SW_SHOW);
  ::UpdateWindow(hwnd_);
  return true;
}

CefRect MainWindow::SurfaceBounds(SurfaceId id) const {
  switch (id) {
    case SurfaceId::kTopbar:
      // Full width, across the very top.
      return CefRect(0, 0, client_width_, layout::kTopbarHeight);

    case SurfaceId::kSidebar: {
      // Below the topbar, down the left edge. Collapsing swaps the full width
      // for the narrow rail, exactly as viewportBounds() assumes.
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

void MainWindow::NotifySurfacesResized() {
  for (size_t i = 0; i < static_cast<size_t>(SurfaceId::kCount); ++i) {
    Layer& target = layers_[i];
    if (target.browser) {
      // Makes CEF re-query GetViewRect and repaint at the new size.
      target.browser->GetHost()->WasResized();
    }
  }
}

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

    case WM_MOUSELEAVE: {
      tracking_mouse_ = false;
      for (size_t i = 0; i < static_cast<size_t>(SurfaceId::kCount); ++i) {
        if (layers_[i].browser) {
          CefMouseEvent event;
          layers_[i].browser->GetHost()->SendMouseMoveEvent(
              event, /*mouseLeave=*/true);
        }
      }
      return 0;
    }

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
