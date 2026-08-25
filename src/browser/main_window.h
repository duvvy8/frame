#ifndef FRAME_BROWSER_MAIN_WINDOW_H_
#define FRAME_BROWSER_MAIN_WINDOW_H_

#include <windows.h>

#include <cstdint>
#include <vector>

#include "include/cef_browser.h"

namespace frame {

// The native top-level window.
//
// Per the architecture: ONE native window per browser window, composited from
// several independently rendered surfaces. Today it hosts a single off-screen
// -rendered surface (the topbar). The sidebar, frame strips, corner masks and
// the real page browser join it in later migration steps, which is why paint
// state is kept per-surface rather than as one window-sized framebuffer.
class MainWindow {
 public:
  struct Options {
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    int width = 1200;
    int height = 800;

    // Show the window without activating it. Dev tooling sets this so that
    // launching Frame on a secondary monitor cannot pull focus away from a
    // full-screen application on the primary one. Off by default — a real
    // browser window should activate normally when the user opens it.
    bool no_activate = false;
  };

  explicit MainWindow(const Options& options);
  ~MainWindow();

  MainWindow(const MainWindow&) = delete;
  MainWindow& operator=(const MainWindow&) = delete;

  bool Create(HINSTANCE instance);

  HWND hwnd() const { return hwnd_; }
  int client_width() const { return client_width_; }
  int client_height() const { return client_height_; }

  // Receives BGRA pixels from CefRenderHandler::OnPaint, on the CEF UI thread.
  void OnTopbarPaint(const void* buffer, int width, int height);

  void SetTopbarBrowser(CefRefPtr<CefBrowser> browser);

 private:
  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);
  void Paint(HDC hdc);

  Options options_;
  HWND hwnd_ = nullptr;
  int client_width_ = 0;
  int client_height_ = 0;

  // BGRA, top-down, exactly as OnPaint delivers it.
  std::vector<uint8_t> topbar_pixels_;
  int topbar_width_ = 0;
  int topbar_height_ = 0;

  CefRefPtr<CefBrowser> topbar_browser_;
};

}  // namespace frame

#endif  // FRAME_BROWSER_MAIN_WINDOW_H_
