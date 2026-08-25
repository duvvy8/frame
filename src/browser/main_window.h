#ifndef FRAME_BROWSER_MAIN_WINDOW_H_
#define FRAME_BROWSER_MAIN_WINDOW_H_

#include <windows.h>

#include <cstdint>
#include <vector>

#include "browser/chrome_surface.h"
#include "include/cef_browser.h"

namespace frame {

// The native top-level window.
//
// ONE native window per browser window, composited from several independently
// rendered surfaces. Today: the topbar and the sidebar. The frame strips,
// corner masks and the real page browser join them in later steps, which is
// why paint state is kept per-surface rather than as one framebuffer.
//
// Every surface rectangle is derived here, from the shared layout constants,
// so no surface can disagree with another about geometry.
class MainWindow {
 public:
  struct Options {
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    int width = 1200;
    int height = 800;

    // Show without activating. Dev tooling sets this so launching Frame on a
    // secondary monitor cannot pull focus from a full-screen application on the
    // primary one. Off by default: a real browser window activates normally.
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

  bool sidebar_open() const { return sidebar_open_; }

  // Where a surface sits in window coordinates. The single place chrome
  // geometry is decided.
  CefRect SurfaceBounds(SurfaceId id) const;

  // From CefRenderHandler::OnPaint, on the CEF UI thread.
  void OnSurfacePaint(SurfaceId id, const void* buffer, int width, int height);

  void SetSurfaceBrowser(SurfaceId id, CefRefPtr<CefBrowser> browser);

 private:
  struct Layer {
    CefRefPtr<CefBrowser> browser;
    std::vector<uint8_t> pixels;  // BGRA, top-down, as OnPaint delivers it.
    int width = 0;
    int height = 0;
  };

  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);

  void Paint(HDC hdc);
  void PaintLayer(HDC hdc, SurfaceId id);
  void NotifySurfacesResized();

  // Input routing.
  void ForwardMouseMove(int x, int y, WPARAM wparam);
  void ForwardMouseButton(int x, int y, WPARAM wparam, CefBrowserHost::MouseButtonType button, bool up);
  void ForwardMouseWheel(int screen_x, int screen_y, int delta, WPARAM wparam);
  bool SurfaceAt(int x, int y, SurfaceId* id, int* local_x, int* local_y) const;

  Layer& layer(SurfaceId id) { return layers_[static_cast<size_t>(id)]; }
  const Layer& layer(SurfaceId id) const {
    return layers_[static_cast<size_t>(id)];
  }

  Options options_;
  HWND hwnd_ = nullptr;
  int client_width_ = 0;
  int client_height_ = 0;
  bool sidebar_open_ = true;
  bool tracking_mouse_ = false;

  Layer layers_[static_cast<size_t>(SurfaceId::kCount)];
};

}  // namespace frame

#endif  // FRAME_BROWSER_MAIN_WINDOW_H_
