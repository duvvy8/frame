#include "browser/main_window.h"

#include <cstring>

#include "include/cef_app.h"
#include "shared/chrome_layout.h"

namespace frame {
namespace {

const wchar_t kWindowClass[] = L"FrameMainWindow";
const wchar_t kWindowTitle[] = L"Frame";

// Shell background behind every surface. Matches the dark shell the chrome
// surfaces are designed against, so uncovered regions do not flash white.
const COLORREF kShellBackground = RGB(0x14, 0x14, 0x16);
const COLORREF kViewportPlaceholder = RGB(0x0d, 0x0d, 0x0f);

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

  // SW_SHOWNOACTIVATE keeps focus where it is. See Options::no_activate.
  ::ShowWindow(hwnd_, options_.no_activate ? SW_SHOWNOACTIVATE : SW_SHOW);
  ::UpdateWindow(hwnd_);
  return true;
}

void MainWindow::SetTopbarBrowser(CefRefPtr<CefBrowser> browser) {
  topbar_browser_ = browser;
}

void MainWindow::OnTopbarPaint(const void* buffer, int width, int height) {
  const size_t bytes = static_cast<size_t>(width) * height * 4;
  if (topbar_pixels_.size() != bytes) {
    topbar_pixels_.resize(bytes);
  }
  memcpy(topbar_pixels_.data(), buffer, bytes);
  topbar_width_ = width;
  topbar_height_ = height;

  if (hwnd_) {
    RECT strip = {0, 0, width, height};
    ::InvalidateRect(hwnd_, &strip, FALSE);
  }
}

void MainWindow::Paint(HDC hdc) {
  // The page viewport region, straight from the shared layout math. Drawing it
  // here proves the C++ port and the chrome surface agree on geometry before
  // any real page browser exists to occupy it.
  const layout::ViewportRect viewport = layout::ViewportBounds(
      {static_cast<double>(client_width_), static_cast<double>(client_height_),
       /*sidebar_open=*/false, /*bookmarks_visible=*/false});

  HBRUSH placeholder = ::CreateSolidBrush(kViewportPlaceholder);
  RECT area = {viewport.x, viewport.y, viewport.x + viewport.width,
               viewport.y + viewport.height};
  ::FillRect(hdc, &area, placeholder);
  ::DeleteObject(placeholder);

  if (topbar_pixels_.empty()) {
    return;
  }

  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = topbar_width_;
  // Negative height: OnPaint delivers a top-down image, GDI defaults to
  // bottom-up. Without this the topbar renders upside down.
  info.bmiHeader.biHeight = -topbar_height_;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;

  ::SetDIBitsToDevice(hdc, 0, 0, topbar_width_, topbar_height_, 0, 0, 0,
                      topbar_height_, topbar_pixels_.data(), &info,
                      DIB_RGB_COLORS);
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
    self = reinterpret_cast<MainWindow*>(
        ::GetWindowLongPtr(hwnd, GWLP_USERDATA));
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

    case WM_SIZE: {
      client_width_ = LOWORD(lparam);
      client_height_ = HIWORD(lparam);
      if (topbar_browser_) {
        // Makes CEF re-query GetViewRect and repaint at the new width.
        topbar_browser_->GetHost()->WasResized();
      }
      return 0;
    }

    case WM_CLOSE:
      if (topbar_browser_) {
        topbar_browser_->GetHost()->CloseBrowser(/*force_close=*/true);
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
