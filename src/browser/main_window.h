#ifndef FRAME_BROWSER_MAIN_WINDOW_H_
#define FRAME_BROWSER_MAIN_WINDOW_H_

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "browser/chrome_surface.h"
#include "browser/corner_mask.h"
#include "include/cef_browser.h"
#include "shared/chrome_layout.h"

namespace frame {

// The native top-level window.
//
// ONE native window per browser window, composited from several independently
// rendered surfaces plus the page.
//
// The chrome surfaces (topbar, sidebar) are off-screen rendered so we own their
// pixels. The PAGE is deliberately not: it is a real child browser window, so
// CEF handles its input, focus, scrolling and IME natively instead of us
// reimplementing all of it.
//
// The window is frameless: Windows draws no caption, and the topbar occupies
// those pixels. Dragging, resizing, maximise and the Snap Layouts flyout are
// reimplemented through hit-testing here. Outer corner rounding stays DWM's,
// which is why OUTER_RADIUS is 0 in the shared constants.
class MainWindow {
 public:
  struct Options {
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    int width = 1200;
    int height = 800;
    bool no_activate = false;
    bool system_titlebar = false;
  };

  // One browser tab and the state the chrome needs to draw it.
  struct Tab {
    int id = 0;
    CefRefPtr<CefBrowser> browser;
    std::string title;
    std::string url;
    bool loading = false;
    bool can_go_back = false;
    bool can_go_forward = false;
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

  // Where a surface sits, in PHYSICAL pixels — for blitting and positioning.
  CefRect SurfaceBounds(SurfaceId id) const;

  // The same rectangle in DIPs, which is the space the layout constants and
  // every HTML surface work in. Keeping the two explicitly separate is what
  // stops a scaled display silently laying the chrome out at raw pixel sizes.
  CefRect SurfaceBoundsDip(SurfaceId id) const;

  // Device scale: 1.0 at 96 DPI, 1.5 at 150%, and so on.
  float DeviceScale() const;

  // Client size in DIPs — the units every CSS surface works in.
  int ClientWidthDip() const;
  int ClientHeightDip() const;
  void OnSurfacePaint(SurfaceId id, const void* buffer, int width, int height);
  void SetSurfaceBrowser(SurfaceId id, CefRefPtr<CefBrowser> browser);

  // --- window commands ---
  void Minimize();
  void ToggleMaximize();
  void CloseWindow();
  bool IsWindowMaximized() const;
  void SetDragExclusions(std::vector<layout::IntRect> regions);

  // --- chrome commands ---
  void ToggleSidebar();

  // --- tabs ---
  int CreateTab(const std::string& url, bool activate);
  void CloseTab(int tab_id);
  void SelectTab(int tab_id);
  // Moves a tab to a new position in the strip. The order here is the order
  // the chrome draws, so this is the only place it changes.
  void ReorderTab(int tab_id, int new_index);

  // --- navigation, applied to the active tab ---
  void Navigate(const std::string& input);
  void GoBack();
  void GoForward();
  void Reload();
  void StopLoad();

  // --- callbacks from PageClient ---
  void OnPageCreated(int tab_id, CefRefPtr<CefBrowser> browser);
  void OnPageClosed(int tab_id);
  void OnPageLoadingChanged(int tab_id,
                            bool loading,
                            bool can_go_back,
                            bool can_go_forward);
  void OnPageLoadError(int tab_id,
                       const std::string& error_text,
                       const std::string& failed_url);
  void OnPageTitleChanged(int tab_id, const std::string& title);
  void OnPageUrlChanged(int tab_id, const std::string& url);

  // Focus follows clicks: an off-screen surface receives no keyboard input
  // unless it is told it has focus.
  void FocusSurface(SurfaceId id);

  // The same payload that gets pushed, so a surface can ask for it on load
  // instead of waiting for a push that may already have happened.
  std::string BrowserStateJson() const { return BuildBrowserStateJson(); }

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
  void ApplyRoundedCorners();
  void PushWindowState();
  void PushShellMetrics();
  void PushBrowserState();
  std::string BuildBrowserStateJson() const;

  LRESULT HitTest(POINT screen_point);
  LRESULT HandleNcCalcSize(WPARAM wparam, LPARAM lparam);

  void ForwardMouseMove(int x, int y, WPARAM wparam);
  void ForwardMouseButton(int x,
                          int y,
                          WPARAM wparam,
                          CefBrowserHost::MouseButtonType button,
                          bool up);
  void ForwardMouseWheel(int screen_x, int screen_y, int delta, WPARAM wparam);
  void ForwardKeyEvent(UINT message, WPARAM wparam, LPARAM lparam);
  void SendMouseLeaveToAll();
  bool SurfaceAt(int x, int y, SurfaceId* id, int* local_x, int* local_y) const;

  // Page placement.
  void LayoutPages();

  int ToPhysical(int dip) const;
  int ToDip(int physical) const;
  layout::ViewportRect ViewportDip() const;
  void UpdateDpi();
  Tab* FindTab(int tab_id);
  const Tab* FindTab(int tab_id) const;
  Tab* ActiveTab();
  CefRefPtr<CefBrowser> ActiveBrowser();

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
  bool tracking_nc_mouse_ = false;

  // Physical dots per inch for the monitor this window is on. CEF makes the
  // process PER_MONITOR_AWARE, so Windows does not scale anything for us and
  // every pixel value we hand it has to be scaled here.
  int dpi_ = 96;

  // Fakes VIEWPORT_RADIUS on the page, which paints its own square corners.
  CornerMask corner_mask_;

  std::vector<layout::IntRect> drag_exclusions_;
  Layer layers_[static_cast<size_t>(SurfaceId::kCount)];

  std::vector<Tab> tabs_;
  int active_tab_id_ = 0;
  int next_tab_id_ = 1;

  // Which chrome surface keyboard input goes to. kCount means "none — the page
  // has it", in which case CEF routes keys to the child window itself.
  SurfaceId focused_surface_ = SurfaceId::kCount;
};

}  // namespace frame

#endif  // FRAME_BROWSER_MAIN_WINDOW_H_
