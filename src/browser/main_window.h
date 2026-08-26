#ifndef FRAME_BROWSER_MAIN_WINDOW_H_
#define FRAME_BROWSER_MAIN_WINDOW_H_

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "browser/chrome_surface.h"
#include "browser/corner_mask.h"
#include "browser/browsing_data.h"
#include "browser/favorites.h"
#include "browser/menu_surface.h"
#include "browser/window_ref.h"
#include "include/cef_browser.h"
#include "include/cef_request_context.h"
#include "shared/chrome_layout.h"
#include "shared/shortcuts.h"
#include "shared/sleep_policy.h"

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
    // An incognito window gets its own in-memory CefRequestContext, so its
    // cookies, cache and storage never touch the profile on disk and are gone
    // when the window closes. This is the isolation itself, not a flag that
    // asks someone else to provide it.
    bool incognito = false;
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

    // --- sleep ---
    //
    // A sleeping tab has NO BROWSER. That is the whole point: the renderer
    // process is gone, not throttled, so the memory it held is actually
    // returned to the system. Everything needed to bring it back is the url
    // and the title, which is why they are kept here rather than read off a
    // browser that no longer exists.
    bool asleep = false;
    // Set between asking a browser to close and its OnBeforeClose arriving, so
    // that callback can tell "this tab is going to sleep" from "this tab is
    // being closed" — the two are the same CEF event and mean opposite things.
    bool sleeping = false;
    // Excluded from automatic sleep. A user decision, never overridden.
    bool never_sleep = false;
    bool muted = false;
    // Steady-clock milliseconds when this tab last stopped being the active
    // one. Zero for a tab that has never been backgrounded.
    unsigned long long backgrounded_at_ms = 0;

    // Answers from the page's own sleep probe, refreshed just before a tab is
    // considered for automatic sleep. They are facts only the renderer knows:
    // CEF's public API has no "is this tab making sound", and nothing outside
    // the page can see that a form has been typed into.
    //
    // Both default to the SAFE value. A tab that has not been probed yet reads
    // as silent and clean, but it also reads as not-yet-idle, so it cannot be
    // slept before its first probe either way.
    bool audible = false;
    bool has_unsaved_input = false;
    // Set while a probe is in flight, so a sweep that lands during one does
    // not stack a second.
    bool probe_pending = false;
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
  bool incognito() const { return options_.incognito; }
  bool fullscreen() const { return fullscreen_; }

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

  // A chrome surface's browser has finished being destroyed. Separate from
  // SetSurfaceBrowser(nullptr) because a close in progress is waiting on it.
  void OnSurfaceClosed(SurfaceId id);

  // The handle every client Frame creates for this window holds instead of a
  // raw pointer. Revoked in the destructor. See window_ref.h.
  CefRefPtr<WindowRef> ref() const { return self_ref_; }

  // --- window commands ---
  void Minimize();
  void ToggleMaximize();
  void CloseWindow();
  void ToggleFullscreen();
  bool IsWindowMaximized() const;
  void SetDragExclusions(std::vector<layout::IntRect> regions);

  // --- chrome commands ---
  void ToggleSidebar();

  // Runs one keyboard command against this window.
  //
  // The single entry point for every shortcut, wherever the key was observed:
  // the page's native child window, an off-screen chrome surface, or this
  // window's own message loop. Three input paths, one implementation — so a
  // shortcut cannot work in one place and not another.
  //
  // Returns true if the command was consumed, which is what the caller passes
  // back to CEF to stop the key reaching web content.
  bool ExecuteCommand(shortcuts::Command command);

  // --- tabs ---
  int CreateTab(const std::string& url, bool activate);
  void CloseTab(int tab_id);
  void SelectTab(int tab_id);
  // Moves a tab to a new position in the strip. The order here is the order
  // the chrome draws, so this is the only place it changes.
  void ReorderTab(int tab_id, int new_index);

  // Strip-relative selection, which is what the keyboard works in: the user
  // presses Ctrl+2 for "the second tab", not for a tab id.
  void SelectTabByIndex(int index);
  void SelectAdjacentTab(int delta);
  void CloseActiveTab();
  void ReopenClosedTab();

  // --- tab operations reachable from the context menu ---
  void DuplicateTab(int tab_id);
  void CloseOtherTabs(int tab_id);
  void CloseTabsToTheRight(int tab_id);
  void ReloadTab(int tab_id);
  void ToggleTabMuted(int tab_id);
  void SetTabNeverSleeps(int tab_id, bool never);
  void CopyTabUrl(int tab_id);

  // --- sleep ---
  //
  // Sleeping DISCARDS the tab's browser rather than throttling it. CEF offers
  // WasHidden() and audio muting, and both were considered: they leave the
  // renderer process running, which means they leave essentially all of the
  // memory allocated. Closing the browser and remembering the url is what
  // Chromium's own tab discarding does, and it is the only version of this
  // feature that can honestly claim to free anything.
  //
  // The cost is the page's in-memory state — scroll position, form contents,
  // JS state — which is why nothing with unsaved input is ever slept
  // automatically. See sleep_policy.h.
  void SleepTab(int tab_id);
  void WakeTab(int tab_id);
  void ToggleTabSleep(int tab_id);
  // Called on a timer; sleeps whatever the policy says is safe to sleep.
  void SweepSleepableTabs();
  // The page's answer to the sleep probe. See ProbeTabForSleep.
  void OnTabSleepProbe(int tab_id, bool audible, bool unsaved_input);

  // Automatic sleep, as the settings page sets it.
  void SetSleepEnabled(bool enabled);
  void SetSleepIdleMs(unsigned long long idle_ms);
  void SetHistoryEnabled(bool enabled) { history_enabled_ = enabled; }
  bool history_enabled() const { return history_enabled_; }
  bool sleep_enabled() const { return sleep_enabled_; }
  unsigned long long sleep_idle_ms() const { return sleep_idle_ms_; }

  // --- context menu ---
  void ShowTabContextMenu(int tab_id, int surface_x_dip, int surface_y_dip);
  void CloseContextMenu();
  bool context_menu_open() const;

  // --- navigation, applied to the active tab ---
  void Navigate(const std::string& input);
  void GoBack();
  void GoForward();
  void Reload();
  void ReloadIgnoringCache();
  void StopLoad();
  void Print();
  void ShowDevTools();
  void AdjustZoom(double steps);  // 0 resets

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
  void OnPageFaviconChanged(int tab_id, const std::string& icon_url);

  // --- favourites ---
  void AddFavorite(const std::string& url, const std::string& title);
  void RemoveFavorite(const std::string& url);
  void MoveFavorite(int from, int to);

  // Focus follows clicks: an off-screen surface receives no keyboard input
  // unless it is told it has focus.
  void FocusSurface(SurfaceId id);

  // Ctrl+L and friends. Gives the sidebar the keyboard, then asks it to put the
  // caret in the address field and select what is there.
  void FocusAddressBar();

  // Bookmarks the active tab. Separate from AddFavorite() because the keyboard
  // has no url to pass — it means "this page, whatever it is".
  void BookmarkActiveTab();

  // The same payload that gets pushed, so a surface can ask for it on load
  // instead of waiting for a push that may already have happened.
  std::string BrowserStateJson() const { return BuildBrowserStateJson(); }

  // Just the pinned sites, for frame://bookmarks. Reads the same store the
  // sidebar draws from, so the two cannot disagree about what is pinned.
  std::string FavoritesJson() const;

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
  void PushPageShellMetrics();
  // Does the work PushBrowserState defers. See the comment there.
  void FlushBrowserState();
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
  bool TryNativeShortcut(WPARAM wparam);
  void SendMouseLeaveToAll();
  bool SurfaceAt(int x, int y, SurfaceId* id, int* local_x, int* local_y) const;

  // --- closing ---
  //
  // Two-phase, and it has to be.
  //
  // WM_CLOSE used to fall through to DefWindowProc, which destroys the window
  // immediately. Every page and surface browser was still alive at that point,
  // so CEF went on calling back into a MainWindow that the window list had
  // already queued for deletion — a use-after-free whose only symptom was an
  // occasional crash on exit, which is the worst kind to chase.
  //
  // Instead: WM_CLOSE starts the close and destroys nothing. Each browser
  // reports in as it finishes, and only when the last one has does the native
  // window actually go. Nothing can call back into a window that no longer
  // exists, because the window outlives every one of its browsers by
  // construction.
  void BeginClose();
  void MaybeFinishClose();
  int LiveBrowserCount() const;

  void OnMenuChoice(const std::string& command, int tab_id);

  // `navigated` separates a NAVIGATION from a title arriving for one. One
  // navigation produces both, and counting both as visits made the history say
  // "3x" for a page opened once.
  void RecordHistory(const Tab& tab, bool navigated);

  sleep::Settings SleepSettings() const;
  void ProbeTabForSleep(int tab_id);
  void UpdateSleepTimer();

  // Page placement.
  void LayoutPages();
  void UpdateCornerMasks();

  // Reads one pixel out of a chrome surface's last rendered bitmap. The shell
  // field is defined in CSS and nowhere else, so the only way for the browser
  // process to learn a colour from it is to look at what was drawn.
  bool SampleSurfacePixel(SurfaceId id,
                          int local_dip_x,
                          int local_dip_y,
                          COLORREF* out) const;
  void ShellCornerColors(const layout::ViewportRect& dip,
                         COLORREF (&out)[CornerMask::kCornerCount]) const;

  // The sidebar's current width in DIPs — animated, so this is the only thing
  // that should ever be asked for it. Reading kSidebarWidth directly would be
  // right twice per transition and wrong throughout it.
  int SidebarWidthDip() const;
  void TickSidebarAnimation();

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

  // Set once WM_CLOSE has been accepted. Everything that could open a tab,
  // start a navigation or push state checks it, so a window on its way out
  // cannot be given more work to finish.
  bool closing_ = false;

  // True only while BeginClose is still deciding what to close. See the
  // comment there: the window must not be destroyed part-way through that.
  bool in_begin_close_ = false;

  // Handed to every client this window creates, and revoked in ~MainWindow.
  CefRefPtr<WindowRef> self_ref_;

  // The sidebar's width WHILE IT IS MOVING.
  //
  // kSidebarWidth and kCollapsedRailWidth are the two resting values; this is
  // everything in between. Held as a double so the easing curve is not
  // quantised to whole pixels before it is drawn — rounding at each step is
  // what turns a smooth 160px slide into a visible staircase.
  double sidebar_width_current_ = layout::kSidebarWidth;
  double sidebar_width_from_ = layout::kSidebarWidth;
  double sidebar_width_to_ = layout::kSidebarWidth;
  unsigned long long sidebar_anim_start_ms_ = 0;

  // Fullscreen is a window state, not a layout constant, so it lives here
  // rather than in the shared geometry: chrome_layout.h describes the chrome
  // Frame has, and in fullscreen there is none to describe.
  bool fullscreen_ = false;
  WINDOWPLACEMENT saved_placement_ = {};
  LONG_PTR saved_style_ = 0;

  // Non-null only for an incognito window. Pages created here are handed this
  // context instead of the global one, which is what keeps their cookies and
  // cache in memory and unshared.
  CefRefPtr<CefRequestContext> request_context_;

  // Ctrl+Shift+T, most recent first. Bounded, because an unbounded undo stack
  // for tabs is a slow leak of every URL the user has ever closed.
  std::vector<std::string> closed_urls_;

  // Browser-state push coalescing. state_push_pending_ means a flush is
  // already queued and further requests can be dropped; last_pushed_state_ is
  // what the surfaces currently believe, so an unchanged state costs nothing.
  bool state_push_pending_ = false;
  std::string last_pushed_state_;

  // Physical dots per inch for the monitor this window is on. CEF makes the
  // process PER_MONITOR_AWARE, so Windows does not scale anything for us and
  // every pixel value we hand it has to be scaled here.
  int dpi_ = 96;

  // Fakes VIEWPORT_RADIUS on the page, which paints its own square corners.
  CornerMask corner_mask_;

  std::vector<layout::IntRect> drag_exclusions_;
  Layer layers_[static_cast<size_t>(SurfaceId::kCount)];

  std::unique_ptr<FavoritesStore> favorites_;
  std::unique_ptr<FaviconCache> favicons_;

  // Automatic sleep. Defaults match sleep_policy.h; the settings page moves
  // them, and SetSleepEnabled restarts the timer so a change takes effect
  // without waiting for the next tick.
  bool sleep_enabled_ = true;
  unsigned long long sleep_idle_ms_ = 30ULL * 60ULL * 1000ULL;

  // Whether visits are written down at all. Separate from the incognito flag:
  // this one is the user's setting for an ordinary window, and incognito
  // overrides it unconditionally in the other direction.
  bool history_enabled_ = true;

  // One menu surface per window, reused for every opening — creating a
  // renderer per right-click would put a visible delay in front of a menu.
  std::unique_ptr<MenuSurface> menu_;

  // Drives automatic sleep. Only running while there is something that could
  // eventually be slept, so an idle window with one tab has no timer at all.
  UINT_PTR sleep_timer_ = 0;

  std::vector<Tab> tabs_;
  int active_tab_id_ = 0;
  int next_tab_id_ = 1;

  // Which chrome surface keyboard input goes to. kCount means "none — the page
  // has it", in which case CEF routes keys to the child window itself.
  SurfaceId focused_surface_ = SurfaceId::kCount;
};

}  // namespace frame

#endif  // FRAME_BROWSER_MAIN_WINDOW_H_
