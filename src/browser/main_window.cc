#include "browser/main_window.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <string>

#include "browser/frame_scheme.h"
#include "browser/page_client.h"
#include "browser/window_list.h"
#include "include/cef_app.h"
#include "include/cef_task.h"
#include "shared/chrome_layout.h"
#include "shared/shortcuts.h"
#include "shared/url_util.h"

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

const COLORREF kShellBackground = RGB(0x0b, 0x0b, 0x0d);
const COLORREF kViewportPlaceholder = RGB(0x00, 0x00, 0x00);

// Drives the sidebar slide. A window timer rather than a posted task, because
// WM_TIMER cannot outlive the window it is set on — a self-reposting task can,
// and would then run against a destroyed MainWindow.
const UINT_PTR kSidebarTimerId = 1;

// Asked for roughly every 8ms; Windows will not go below about 10. Progress is
// computed from the clock rather than from the number of ticks, so a late or
// coalesced timer message shortens the animation instead of stretching it.
const UINT kSidebarTimerMs = 8;

// A new tab opens on Frame's own page, not a blank one.

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

int KeyboardModifiers(WPARAM wparam, LPARAM lparam) {
  int modifiers = 0;
  if (::GetKeyState(VK_SHIFT) < 0) {
    modifiers |= EVENTFLAG_SHIFT_DOWN;
  }
  if (::GetKeyState(VK_CONTROL) < 0) {
    modifiers |= EVENTFLAG_CONTROL_DOWN;
  }
  if (::GetKeyState(VK_MENU) < 0) {
    modifiers |= EVENTFLAG_ALT_DOWN;
  }
  if (::GetKeyState(VK_CAPITAL) & 1) {
    modifiers |= EVENTFLAG_CAPS_LOCK_ON;
  }
  if (::GetKeyState(VK_NUMLOCK) & 1) {
    modifiers |= EVENTFLAG_NUM_LOCK_ON;
  }
  // Bit 24 of lParam marks the extended keys: right-hand modifiers, the arrow
  // cluster, and the numpad Enter.
  if ((lparam >> 24) & 1) {
    modifiers |= EVENTFLAG_IS_KEY_PAD;
  }
  return modifiers;
}

// The omnibox rule and JSON escaping live in shared/url_util.h so they can be
// tested directly; both handle input Frame does not control.
using frame::url::JsonEscape;
using frame::url::NormalizeUrl;

// Defers a navigation to the next turn of the UI thread.
//
// Needed because OnLoadError arrives while the failed navigation is still
// unwinding: starting another one from inside that handler re-enters the
// teardown and takes the browser process down with it.
class LoadUrlTask : public CefTask {
 public:
  LoadUrlTask(CefRefPtr<CefFrame> frame, const std::string& url)
      : frame_(frame), url_(url) {}

  void Execute() override {
    if (frame_) {
      frame_->LoadURL(url_);
    }
  }

 private:
  CefRefPtr<CefFrame> frame_;
  const std::string url_;

  IMPLEMENT_REFCOUNTING(LoadUrlTask);
  DISALLOW_COPY_AND_ASSIGN(LoadUrlTask);
};

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

  // WS_OVERLAPPEDWINDOW is kept even though no caption is drawn: the style is
  // what gives the window snapping, minimise/restore animations and sane
  // taskbar behaviour. WM_NCCALCSIZE removes the visible frame without giving
  // those up.
  hwnd_ = ::CreateWindowExW(0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW,
                            options_.x, options_.y, options_.width,
                            options_.height, nullptr, nullptr, instance, this);
  if (!hwnd_) {
    return false;
  }

  UpdateDpi();
  ApplyRoundedCorners();
  corner_mask_.Create(hwnd_, instance);

  if (options_.incognito) {
    // An empty cache_path is what makes the context in-memory. Nothing this
    // window loads is written to the profile, and the whole context is
    // discarded when the last reference to it goes with the window.
    CefRequestContextSettings context_settings;
    // The handler is what teaches this context about frame://. Without it the
    // window opens on ERR_UNKNOWN_URL_SCHEME instead of the new tab page,
    // because scheme handler factories are per-context and the one installed at
    // startup belongs to the global context only.
    request_context_ = CefRequestContext::CreateContext(
        context_settings, CreateSchemeContextHandler());
  }

  const std::string profile = ProfileDir();
  favorites_.reset(new FavoritesStore(profile + "\\favorites.txt"));
  favorites_->Load();
  favorites_->EnsureDefaults();

  favicons_.reset(new FaviconCache(profile + "\\favicons"));
  favicons_->Load();

  // Ask each pinned site for its own icon, once. Nothing is asked of anyone
  // else — no third-party favicon service ever sees this list.
  for (const Favorite& item : favorites_->items()) {
    const std::string host = HostOf(item.url);
    if (host.empty() || !favicons_->DataUrl(host).empty()) {
      continue;
    }
    favicons_->Fetch(host, "https://" + host + "/favicon.ico",
                     [this]() { PushBrowserState(); });
  }

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
  // The outer corner belongs to DWM, never to us, so the curve matches every
  // other Windows 11 window and cannot come apart from the window's own
  // background at the corner.
  UINT preference = DWMWCP_ROUND;
  ::DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &preference,
                          sizeof(preference));

  BOOL dark = TRUE;
  ::DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark,
                          sizeof(dark));
}

// --- DPI ------------------------------------------------------------------
//
// CEF makes the process PER_MONITOR_AWARE, which means Windows scales nothing
// for us. Every layout constant is a DIP, so all of them have to be converted
// before they touch a pixel — a window rectangle, a blit, a hit test. Leaving
// them raw is invisible at 100% and wrong everywhere else.

int MainWindow::ToPhysical(int dip) const {
  return ::MulDiv(dip, dpi_, 96);
}

int MainWindow::ToDip(int physical) const {
  return ::MulDiv(physical, 96, dpi_);
}

float MainWindow::DeviceScale() const {
  return static_cast<float>(dpi_) / 96.0f;
}

int MainWindow::ClientWidthDip() const {
  return ToDip(client_width_);
}

int MainWindow::ClientHeightDip() const {
  return ToDip(client_height_);
}

void MainWindow::UpdateDpi() {
  if (!hwnd_) {
    return;
  }
  const UINT dpi = ::GetDpiForWindow(hwnd_);
  dpi_ = dpi > 0 ? static_cast<int>(dpi) : 96;
}

layout::ViewportRect MainWindow::ViewportDip() const {
  if (fullscreen_) {
    // The whole client area, square-cornered. Not expressed through
    // ViewportBounds(): that function is a verified 1:1 port of the Electron
    // build's geometry, and fullscreen is a state the original never had.
    // Teaching it a new case would break the parity it exists to guarantee.
    return {0, 0, ClientWidthDip(), ClientHeightDip(), 0};
  }
  // Computed in DIPs so the ported geometry stays exactly the function that
  // was verified against the original, rather than a scaled variant of it.
  layout::ViewportRect rect =
      layout::ViewportBounds({static_cast<double>(ClientWidthDip()),
                              static_cast<double>(ClientHeightDip()),
                              sidebar_open_,
                              /*bookmarks_visible=*/false});

  // ViewportBounds knows the two RESTING positions and is left that way — it is
  // a verified 1:1 port and teaching it to interpolate would put a transition
  // inside the one function that must not drift from the original.
  //
  // So the in-between is applied here instead. The right edge does not move, so
  // it is taken from the rect above rather than recomputed, and only the left
  // edge follows the sidebar.
  const int animated_x = SidebarWidthDip();
  if (animated_x != rect.x) {
    const int right = rect.x + rect.width;
    rect.x = animated_x;
    rect.width = std::max(0, right - animated_x);
  }
  return rect;
}

CefRect MainWindow::SurfaceBoundsDip(SurfaceId id) const {
  switch (id) {
    case SurfaceId::kTopbar:
      return CefRect(0, 0, ClientWidthDip(), layout::kTopbarHeight);

    case SurfaceId::kSidebar: {
      const int width = SidebarWidthDip();
      const int height = ClientHeightDip() - layout::kTopbarHeight;
      return CefRect(0, layout::kTopbarHeight, width, height > 0 ? height : 0);
    }

    default:
      return CefRect(0, 0, 0, 0);
  }
}

CefRect MainWindow::SurfaceBounds(SurfaceId id) const {
  const CefRect dip = SurfaceBoundsDip(id);
  return CefRect(ToPhysical(dip.x), ToPhysical(dip.y), ToPhysical(dip.width),
                 ToPhysical(dip.height));
}

void MainWindow::SetSurfaceBrowser(SurfaceId id,
                                   CefRefPtr<CefBrowser> browser) {
  layer(id).browser = browser;
  if (browser) {
    PushShellMetrics();
    PushWindowState();
    PushBrowserState();
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

int MainWindow::SidebarWidthDip() const {
  return static_cast<int>(std::lround(sidebar_width_current_));
}

void MainWindow::ToggleSidebar() {
  sidebar_open_ = !sidebar_open_;

  // Animated, not switched. This used to jump 160px in one frame, which is the
  // largest single movement in the whole UI and the one place it was most
  // obviously missing. kSidebarTransitionMs has been in the shared constants
  // since the port — it was the duration of exactly this transition in the
  // Electron build, and nothing had used it until now.
  sidebar_width_from_ = sidebar_width_current_;
  sidebar_width_to_ = sidebar_open_ ? layout::kSidebarWidth
                                    : layout::kCollapsedRailWidth;
  sidebar_anim_start_ms_ = ::GetTickCount64();

  // Re-arming an existing timer restarts it, so toggling mid-slide picks up
  // from wherever it had reached instead of snapping back to the start.
  ::SetTimer(hwnd_, kSidebarTimerId, kSidebarTimerMs, nullptr);

  // The chrome's own idea of open/closed flips immediately: the sidebar's
  // contents should be fading to their new state WHILE it moves, not after it
  // arrives.
  PushBrowserState();
}

void MainWindow::TickSidebarAnimation() {
  const ULONGLONG now = ::GetTickCount64();
  const ULONGLONG elapsed = now - sidebar_anim_start_ms_;

  double t = static_cast<double>(elapsed) /
             static_cast<double>(layout::kSidebarTransitionMs);
  if (t >= 1.0) {
    t = 1.0;
    ::KillTimer(hwnd_, kSidebarTimerId);
  }

  // Ease-out cubic: leaves immediately, arrives gently. The same shape as the
  // --ease curve the stylesheets use, so native movement and CSS movement
  // decelerate together rather than one finishing visibly before the other.
  const double eased = 1.0 - std::pow(1.0 - t, 3.0);
  sidebar_width_current_ =
      sidebar_width_from_ + (sidebar_width_to_ - sidebar_width_from_) * eased;

  // Everything that depends on the sidebar's width, every frame. The page is
  // moved and resized with it, and the shell field is re-anchored on both the
  // chrome surfaces and the page, so the gradient stays continuous across the
  // seam WHILE it slides rather than only once it stops.
  NotifySurfacesResized();
  PushShellMetrics();
  PushPageShellMetrics();
  LayoutPages();
  ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::ToggleFullscreen() {
  if (!hwnd_) {
    return;
  }

  if (!fullscreen_) {
    // Remember enough to put the window back exactly where it was — the style
    // as well as the placement, or restoring lands a styleless window at the
    // right coordinates.
    saved_placement_.length = sizeof(saved_placement_);
    ::GetWindowPlacement(hwnd_, &saved_placement_);
    saved_style_ = ::GetWindowLongPtr(hwnd_, GWL_STYLE);

    MONITORINFO monitor = {sizeof(monitor)};
    if (!::GetMonitorInfo(::MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST),
                          &monitor)) {
      return;
    }

    fullscreen_ = true;
    // rcMonitor, not rcWork: fullscreen covers the taskbar too.
    ::SetWindowLongPtr(hwnd_, GWL_STYLE, saved_style_ & ~WS_OVERLAPPEDWINDOW);
    ::SetWindowPos(hwnd_, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                   monitor.rcMonitor.right - monitor.rcMonitor.left,
                   monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                   SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
  } else {
    fullscreen_ = false;
    ::SetWindowLongPtr(hwnd_, GWL_STYLE, saved_style_);
    ::SetWindowPlacement(hwnd_, &saved_placement_);
    ::SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER |
                       SWP_FRAMECHANGED | SWP_NOACTIVATE);
  }

  // The chrome does not shrink — it stops being drawn and stops being hit-
  // tested, and the page takes the whole client area. Keeping the surfaces at
  // their real size means leaving fullscreen needs no re-render, so there is no
  // blank flash on the way back out.
  NotifySurfacesResized();
  PushShellMetrics();
  LayoutPages();
  ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::FocusAddressBar() {
  if (fullscreen_) {
    // There is no address bar on screen to focus.
    return;
  }
  if (!sidebar_open_) {
    ToggleSidebar();
  }
  FocusSurface(SurfaceId::kSidebar);

  Layer& sidebar = layer(SurfaceId::kSidebar);
  if (!sidebar.browser) {
    return;
  }
  CefRefPtr<CefFrame> main_frame = sidebar.browser->GetMainFrame();
  if (main_frame) {
    main_frame->ExecuteJavaScript(
        "window.FrameShell && FrameShell.onFocusAddress();",
        main_frame->GetURL(), 0);
  }
}

void MainWindow::BookmarkActiveTab() {
  const Tab* active = FindTab(active_tab_id_);
  if (!active || active->url.empty() ||
      frame::url::StartsWith(active->url, "frame://")) {
    // Frame's own pages are always reachable; pinning one is clutter.
    return;
  }
  AddFavorite(active->url, active->title);
}

bool MainWindow::ExecuteCommand(shortcuts::Command command) {
  using Command = shortcuts::Command;

  const int tab_index = shortcuts::SelectedTabIndex(command);
  if (tab_index >= 0) {
    SelectTabByIndex(tab_index);
    return true;
  }

  switch (command) {
    case Command::kNone:
      return false;

    // --- tabs ---
    case Command::kNewTab:
      CreateTab(std::string(), /*activate=*/true);
      return true;
    case Command::kCloseTab:
      CloseActiveTab();
      return true;
    case Command::kReopenTab:
      ReopenClosedTab();
      return true;
    case Command::kNextTab:
      SelectAdjacentTab(1);
      return true;
    case Command::kPrevTab:
      SelectAdjacentTab(-1);
      return true;
    case Command::kSelectLastTab:
      SelectTabByIndex(static_cast<int>(tabs_.size()) - 1);
      return true;

    // --- windows ---
    case Command::kNewWindow:
    case Command::kNewIncognitoWindow: {
      Options fresh;
      fresh.incognito = command == Command::kNewIncognitoWindow;
      // Offset from this window so the new one does not land exactly on top of
      // it and look like nothing happened.
      RECT bounds = {};
      if (::GetWindowRect(hwnd_, &bounds)) {
        fresh.x = bounds.left + ToPhysical(28);
        fresh.y = bounds.top + ToPhysical(28);
        fresh.width = bounds.right - bounds.left;
        fresh.height = bounds.bottom - bounds.top;
      }
      fresh.system_titlebar = options_.system_titlebar;
      if (MainWindow* opened = windows::Open(fresh)) {
        opened->CreateTab(frame::url::kNewTabPage, /*activate=*/true);
      }
      return true;
    }
    case Command::kCloseWindow:
      CloseWindow();
      return true;
    case Command::kToggleFullscreen:
      ToggleFullscreen();
      return true;

    // --- navigation ---
    case Command::kBack:
      GoBack();
      return true;
    case Command::kForward:
      GoForward();
      return true;
    case Command::kReload:
      Reload();
      return true;
    case Command::kReloadHard:
      ReloadIgnoringCache();
      return true;
    case Command::kStop:
      // Escape is only ours while something is actually loading. Otherwise it
      // belongs to the page, which uses it to dismiss its own dialogs.
      if (const Tab* active = FindTab(active_tab_id_)) {
        if (active->loading) {
          StopLoad();
          return true;
        }
      }
      return false;
    case Command::kHomePage:
      Navigate(frame::url::kNewTabPage);
      return true;

    // --- chrome ---
    case Command::kFocusAddress:
      FocusAddressBar();
      return true;
    case Command::kToggleSidebar:
      ToggleSidebar();
      return true;
    case Command::kBookmarkPage:
      BookmarkActiveTab();
      return true;
    case Command::kOpenDownloads:
      CreateTab("frame://downloads", /*activate=*/true);
      return true;
    case Command::kOpenHistory:
      CreateTab("frame://history", /*activate=*/true);
      return true;
    case Command::kOpenSettings:
      CreateTab("frame://settings", /*activate=*/true);
      return true;
    case Command::kDevTools:
      ShowDevTools();
      return true;
    case Command::kPrint:
      Print();
      return true;

    // --- zoom ---
    case Command::kZoomIn:
      AdjustZoom(1.0);
      return true;
    case Command::kZoomOut:
      AdjustZoom(-1.0);
      return true;
    case Command::kZoomReset:
      AdjustZoom(0.0);
      return true;

    // --- editing ---
    //
    // Never handled here. On a page Chromium already implements all of these
    // natively, and the off-screen chrome surfaces route them straight to
    // CefFrame in ChromeSurface::OnPreKeyEvent, where the target frame is
    // known. Claiming them at window level would break both.
    case Command::kCopy:
    case Command::kCut:
    case Command::kPaste:
    case Command::kSelectAll:
    case Command::kUndo:
    case Command::kRedo:
      return false;

    default:
      return false;
  }
}

// --- tabs -----------------------------------------------------------------

MainWindow::Tab* MainWindow::FindTab(int tab_id) {
  for (Tab& tab : tabs_) {
    if (tab.id == tab_id) {
      return &tab;
    }
  }
  return nullptr;
}

const MainWindow::Tab* MainWindow::FindTab(int tab_id) const {
  for (const Tab& tab : tabs_) {
    if (tab.id == tab_id) {
      return &tab;
    }
  }
  return nullptr;
}

MainWindow::Tab* MainWindow::ActiveTab() {
  return FindTab(active_tab_id_);
}

CefRefPtr<CefBrowser> MainWindow::ActiveBrowser() {
  Tab* tab = ActiveTab();
  return tab ? tab->browser : nullptr;
}

int MainWindow::CreateTab(const std::string& url, bool activate) {
  Tab tab;
  tab.id = next_tab_id_++;
  tab.url = url.empty() ? frame::url::kNewTabPage : url;
  tab.title = "New Tab";
  tab.loading = true;
  tabs_.push_back(tab);

  if (activate) {
    active_tab_id_ = tab.id;
  }

  const layout::ViewportRect viewport = layout::ViewportBounds(
      {static_cast<double>(client_width_), static_cast<double>(client_height_),
       sidebar_open_, /*bookmarks_visible=*/false});

  CefWindowInfo window_info;
  // A real child window, not off-screen: CEF owns the page's input and focus.
  window_info.SetAsChild(hwnd_, CefRect(viewport.x, viewport.y,
                                        viewport.width > 0 ? viewport.width : 1,
                                        viewport.height > 0 ? viewport.height
                                                            : 1));

  // ALLOY, not the default.
  //
  // SetAsChild leaves runtime_style at CEF_RUNTIME_STYLE_DEFAULT, which means
  // CHROME style — "the full Chrome UI and browser functionality". Frame draws
  // its own chrome and uses none of it, but it was being built anyway: with a
  // single tab open the process list held five renderers, and two of them were
  // chrome://omnibox-popup.top-chrome, an address-bar dropdown belonging to a
  // UI this browser does not have.
  //
  // Alloy is the style the off-screen chrome surfaces already use —
  // SetAsWindowless sets it unconditionally — so this makes the page browsers
  // consistent with them rather than introducing something new.
  window_info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;

  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(255, 0, 0, 0);

  CefRefPtr<PageClient> client(new PageClient(this, tab.id));
  // request_context_ is null for an ordinary window, which means the global
  // context — exactly the behaviour before incognito existed.
  CefBrowserHost::CreateBrowser(window_info, client, NormalizeUrl(tab.url),
                                settings, nullptr, request_context_);

  PushBrowserState();
  return tab.id;
}

void MainWindow::CloseTab(int tab_id) {
  Tab* tab = FindTab(tab_id);
  if (!tab) {
    return;
  }

  // Remembered here rather than in OnPageClosed, which also runs when the whole
  // window is being torn down — reopening tabs into a window that is going away
  // is not something to offer. An incognito window remembers nothing at all:
  // that is the entire point of it.
  //
  // StartsWith, not equality. A loaded page reports its URL back through
  // OnAddressChange in Chromium's normalised form, which for a standard scheme
  // means a trailing slash — so the tab's url is "frame://newtab/" and never
  // equals the "frame://newtab" constant it was opened with. Comparing them
  // directly silently recorded every new tab page, and Ctrl+Shift+T reopened
  // blank tabs while pushing the URLs actually worth restoring off the stack.
  const bool is_throwaway =
      frame::url::StartsWith(tab->url, frame::url::kNewTabPage) ||
      frame::url::StartsWith(tab->url, frame::url::kBlankPage);

  if (!options_.incognito && !tab->url.empty() && !is_throwaway) {
    closed_urls_.insert(closed_urls_.begin(), tab->url);
    constexpr size_t kMaxClosedUrls = 16;
    if (closed_urls_.size() > kMaxClosedUrls) {
      closed_urls_.resize(kMaxClosedUrls);
    }
  }

  if (tab->browser) {
    // OnPageClosed finishes the bookkeeping once CEF has actually torn it down.
    tab->browser->GetHost()->CloseBrowser(/*force_close=*/true);
  } else {
    // Never got as far as a browser; drop it directly.
    OnPageClosed(tab_id);
  }
}

void MainWindow::SelectTab(int tab_id) {
  if (!FindTab(tab_id) || active_tab_id_ == tab_id) {
    return;
  }
  active_tab_id_ = tab_id;
  LayoutPages();
  PushBrowserState();

  // Clicking a tab should leave the keyboard with the page, not the chrome.
  FocusSurface(SurfaceId::kCount);
  if (CefRefPtr<CefBrowser> browser = ActiveBrowser()) {
    browser->GetHost()->SetFocus(true);
  }
}

void MainWindow::SelectTabByIndex(int index) {
  // Silently ignored when the strip is shorter than the number pressed, which
  // is what every other browser does: Ctrl+6 with four tabs open is a
  // near-miss, not a request to do something else.
  if (index < 0 || static_cast<size_t>(index) >= tabs_.size()) {
    return;
  }
  SelectTab(tabs_[static_cast<size_t>(index)].id);
}

void MainWindow::SelectAdjacentTab(int delta) {
  if (tabs_.size() < 2) {
    return;
  }
  size_t current = 0;
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (tabs_[i].id == active_tab_id_) {
      current = i;
      break;
    }
  }
  // Wraps in both directions. Ctrl+Tab on the last tab returning to the first
  // is the behaviour everywhere else, and stopping at the end feels broken.
  const int count = static_cast<int>(tabs_.size());
  int next = (static_cast<int>(current) + delta) % count;
  if (next < 0) {
    next += count;
  }
  SelectTab(tabs_[static_cast<size_t>(next)].id);
}

void MainWindow::CloseActiveTab() {
  if (active_tab_id_ != 0) {
    CloseTab(active_tab_id_);
  }
}

void MainWindow::ReopenClosedTab() {
  if (closed_urls_.empty()) {
    return;
  }
  const std::string url = closed_urls_.front();
  closed_urls_.erase(closed_urls_.begin());
  CreateTab(url, /*activate=*/true);
}

void MainWindow::ReorderTab(int tab_id, int new_index) {
  size_t from = tabs_.size();
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (tabs_[i].id == tab_id) {
      from = i;
      break;
    }
  }
  if (from == tabs_.size() || tabs_.empty()) {
    return;
  }

  // The surface computes the target from pointer position, so clamp rather
  // than trust it.
  const int last = static_cast<int>(tabs_.size()) - 1;
  const int to = std::max(0, std::min(new_index, last));
  if (static_cast<size_t>(to) == from) {
    return;
  }

  Tab moved = tabs_[from];
  tabs_.erase(tabs_.begin() + from);
  tabs_.insert(tabs_.begin() + to, moved);

  // No page moves and no browser is touched: only the strip order changed.
  PushBrowserState();
}

void MainWindow::LayoutPages() {
  const layout::ViewportRect dip = ViewportDip();
  const layout::ViewportRect viewport = {
      ToPhysical(dip.x), ToPhysical(dip.y), ToPhysical(dip.width),
      ToPhysical(dip.height), ToPhysical(dip.radius)};

  for (Tab& tab : tabs_) {
    if (!tab.browser) {
      continue;
    }
    HWND page = tab.browser->GetHost()->GetWindowHandle();
    if (!page) {
      continue;
    }
    if (tab.id != active_tab_id_) {
      ::ShowWindow(page, SW_HIDE);
      continue;
    }
    ::SetWindowPos(page, nullptr, viewport.x, viewport.y, viewport.width,
                   viewport.height, SWP_NOZORDER | SWP_NOACTIVATE);
    ::ShowWindow(page, SW_SHOW);
  }

  // The page keeps its square corners and the masks cover them. Clipping the
  // page with SetWindowRgn instead produced a visibly stair-stepped curve,
  // because an HRGN is a binary mask with no antialiasing.
  //
  // In fullscreen there is no rounding to fake, and a mask left on screen would
  // be four grey blocks over the corners of the video.
  UpdateCornerMasks();
}

void MainWindow::UpdateCornerMasks() {
  if (fullscreen_ || !ActiveBrowser()) {
    corner_mask_.Hide();
    return;
  }

  const layout::ViewportRect dip = ViewportDip();
  const layout::ViewportRect viewport = {
      ToPhysical(dip.x), ToPhysical(dip.y), ToPhysical(dip.width),
      ToPhysical(dip.height), ToPhysical(dip.radius)};

  COLORREF corner_colors[CornerMask::kCornerCount];
  ShellCornerColors(dip, corner_colors);
  corner_mask_.Layout(viewport, corner_colors);

  if (Tab* active = ActiveTab()) {
    if (active->browser) {
      corner_mask_.RaiseAbove(active->browser->GetHost()->GetWindowHandle());
    }
  }
}

bool MainWindow::SampleSurfacePixel(SurfaceId id,
                                    int local_dip_x,
                                    int local_dip_y,
                                    COLORREF* out) const {
  const Layer& source = layer(id);
  if (source.pixels.empty() || source.width <= 0 || source.height <= 0) {
    return false;
  }
  // The stored bitmap is in physical pixels; the caller is working in DIPs,
  // like the rest of the layout code.
  const float scale = DeviceScale();
  const int x = static_cast<int>(local_dip_x * scale);
  const int y = static_cast<int>(local_dip_y * scale);
  if (x < 0 || y < 0 || x >= source.width || y >= source.height) {
    return false;
  }
  const size_t index =
      (static_cast<size_t>(y) * static_cast<size_t>(source.width) +
       static_cast<size_t>(x)) * 4;
  if (index + 3 >= source.pixels.size()) {
    return false;
  }
  // BGRA, as OnPaint delivers it.
  *out = RGB(source.pixels[index + 2], source.pixels[index + 1],
             source.pixels[index]);
  return true;
}

void MainWindow::ShellCornerColors(
    const layout::ViewportRect& dip,
    COLORREF (&out)[CornerMask::kCornerCount]) const {
  // kShellBackground is the right answer for three of the four corners, and it
  // is right by construction rather than by luck: shell.css settles the field
  // back to exactly --bg along the window's right and bottom edges, because the
  // eight-pixel margin the window paints there with a GDI brush cannot hold a
  // gradient. Every corner touching those edges is therefore already flat.
  for (COLORREF& color : out) {
    color = kShellBackground;
  }

  // The top-left corner is the exception, and the only one that ever showed a
  // black wedge: it sits inland, in the brightest part of the field.
  //
  // The colour is READ from the sidebar's rendered pixels rather than computed
  // here. The field is defined once, in CSS, and re-deriving it in C++ would
  // make this the second place that decides what the shell looks like — so the
  // two would drift the first time a colour changed. Sampling cannot drift.
  //
  // The probe sits in the sidebar's right margin, a few pixels in from its edge
  // and clear of the address pill and the favourites, so what comes back is the
  // field and not a control that happens to be in the way.
  const CefRect sidebar = SurfaceBoundsDip(SurfaceId::kSidebar);
  if (sidebar.width < 6 || sidebar.height < 12) {
    return;
  }
  constexpr int kProbeInset = 4;
  const int probe_x = sidebar.width - kProbeInset;

  COLORREF sampled = 0;
  if (SampleSurfacePixel(SurfaceId::kSidebar, probe_x,
                         (dip.y - sidebar.y) + kProbeInset, &sampled)) {
    out[0] = sampled;  // CornerMask::kTopLeft
  }
  if (SampleSurfacePixel(SurfaceId::kSidebar, probe_x,
                         (dip.y + dip.height - sidebar.y) - kProbeInset,
                         &sampled)) {
    out[2] = sampled;  // CornerMask::kBottomLeft
  }
}

// --- navigation -----------------------------------------------------------

void MainWindow::Navigate(const std::string& input) {
  const std::string url = NormalizeUrl(input);
  if (CefRefPtr<CefBrowser> browser = ActiveBrowser()) {
    browser->GetMainFrame()->LoadURL(url);
    return;
  }
  // Nothing to navigate: opening a tab is what the user meant.
  CreateTab(url, /*activate=*/true);
}

void MainWindow::GoBack() {
  if (CefRefPtr<CefBrowser> browser = ActiveBrowser()) {
    browser->GoBack();
  }
}

void MainWindow::GoForward() {
  if (CefRefPtr<CefBrowser> browser = ActiveBrowser()) {
    browser->GoForward();
  }
}

void MainWindow::Reload() {
  if (CefRefPtr<CefBrowser> browser = ActiveBrowser()) {
    browser->Reload();
  }
}

void MainWindow::ReloadIgnoringCache() {
  if (CefRefPtr<CefBrowser> browser = ActiveBrowser()) {
    browser->ReloadIgnoreCache();
  }
}

void MainWindow::StopLoad() {
  if (CefRefPtr<CefBrowser> browser = ActiveBrowser()) {
    browser->StopLoad();
  }
}

void MainWindow::Print() {
  if (CefRefPtr<CefBrowser> browser = ActiveBrowser()) {
    browser->GetHost()->Print();
  }
}

void MainWindow::ShowDevTools() {
  CefRefPtr<CefBrowser> browser = ActiveBrowser();
  if (!browser) {
    return;
  }
  // A plain popup window. DevTools is a developer surface, not part of Frame's
  // chrome, so it deliberately does not get the frameless treatment.
  CefWindowInfo window_info;
  window_info.SetAsPopup(hwnd_, "Frame DevTools");
  browser->GetHost()->ShowDevTools(window_info, /*client=*/nullptr,
                                   CefBrowserSettings(), CefPoint());
}

void MainWindow::AdjustZoom(double steps) {
  CefRefPtr<CefBrowser> browser = ActiveBrowser();
  if (!browser) {
    return;
  }
  if (steps == 0.0) {
    browser->GetHost()->SetZoomLevel(0.0);
    return;
  }
  // Chromium's zoom "level" is logarithmic — factor = 1.2^level — so a fixed
  // step is a fixed PERCENTAGE change at every size, which is what makes
  // repeated presses feel even. Clamped to roughly 25%..500%, matching the ends
  // of Chrome's own range.
  constexpr double kStep = 0.5;
  constexpr double kMinLevel = -7.6;
  constexpr double kMaxLevel = 8.8;
  const double level = browser->GetHost()->GetZoomLevel() + steps * kStep;
  browser->GetHost()->SetZoomLevel(std::max(kMinLevel, std::min(kMaxLevel, level)));
}

// --- page callbacks -------------------------------------------------------

void MainWindow::OnPageCreated(int tab_id, CefRefPtr<CefBrowser> browser) {
  Tab* tab = FindTab(tab_id);
  if (!tab) {
    // The tab was closed before its browser finished being created.
    browser->GetHost()->CloseBrowser(/*force_close=*/true);
    return;
  }
  tab->browser = browser;
  LayoutPages();
  PushBrowserState();
}

void MainWindow::OnPageClosed(int tab_id) {
  const bool was_active = (tab_id == active_tab_id_);
  size_t index = tabs_.size();
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (tabs_[i].id == tab_id) {
      index = i;
      break;
    }
  }
  if (index == tabs_.size()) {
    return;
  }
  tabs_.erase(tabs_.begin() + index);

  if (was_active) {
    // Activate the neighbour that took its place, falling back to the last tab.
    if (tabs_.empty()) {
      active_tab_id_ = 0;
    } else {
      const size_t next = std::min(index, tabs_.size() - 1);
      active_tab_id_ = tabs_[next].id;
    }
    LayoutPages();
  }

  PushBrowserState();

  // Closing the last tab closes the window, matching every other browser.
  if (tabs_.empty() && hwnd_) {
    ::PostMessage(hwnd_, WM_CLOSE, 0, 0);
  }
}

void MainWindow::OnPageLoadingChanged(int tab_id,
                                      bool loading,
                                      bool can_go_back,
                                      bool can_go_forward) {
  Tab* tab = FindTab(tab_id);
  if (!tab) {
    return;
  }
  tab->loading = loading;
  tab->can_go_back = can_go_back;
  tab->can_go_forward = can_go_forward;

  // A frame:// page that finished loading while the sidebar was collapsed has
  // the open-sidebar fallback baked into its stylesheet and needs correcting
  // once, here. Nothing is pushed to third-party pages.
  if (!loading && !sidebar_open_) {
    PushPageShellMetrics();
  }
  PushBrowserState();
}

void MainWindow::OnPageLoadError(int tab_id,
                                 const std::string& error_text,
                                 const std::string& failed_url) {
  Tab* tab = FindTab(tab_id);
  if (!tab) {
    return;
  }
  tab->loading = false;

  // Never replace our own error page with itself: if frame://unreachable
  // somehow fails, showing whatever CEF reports is better than looping.
  if (frame::url::StartsWith(failed_url, "frame://unreachable")) {
    PushBrowserState();
    return;
  }

  // Only outright navigation failures land here — PageClient already filters
  // to main-frame loads and drops ERR_ABORTED. A site's own 404 page is the
  // site's business and is never replaced by this.
  const std::string target = std::string("frame://unreachable?url=") +
                             frame::url::UrlEncode(failed_url) +
                             "&reason=" + frame::url::UrlEncode(error_text);

  if (tab->browser) {
    // POSTED, never called directly from here. OnLoadError arrives while the
    // failed navigation is still unwinding, and starting another one from
    // inside it re-enters that teardown — which crashes the browser process
    // outright rather than failing gracefully.
    CefRefPtr<CefFrame> main_frame = tab->browser->GetMainFrame();
    if (main_frame) {
      CefPostTask(TID_UI, new LoadUrlTask(main_frame, target));
    }
  }
  PushBrowserState();
}

void MainWindow::OnPageFaviconChanged(int tab_id, const std::string& icon_url) {
  Tab* tab = FindTab(tab_id);
  if (!tab || !favicons_) {
    return;
  }
  // A site declaring its own icon is the best source for it, so this is
  // preferred over guessing at /favicon.ico.
  const std::string host = HostOf(tab->url);
  if (host.empty()) {
    return;
  }
  favicons_->Fetch(host, icon_url, [this]() { PushBrowserState(); });
}

void MainWindow::AddFavorite(const std::string& url, const std::string& title) {
  if (!favorites_) {
    return;
  }
  favorites_->Add(url, title);
  const std::string host = HostOf(url);
  if (favicons_ && !host.empty() && favicons_->DataUrl(host).empty()) {
    favicons_->Fetch(host, "https://" + host + "/favicon.ico",
                     [this]() { PushBrowserState(); });
  }
  PushBrowserState();
}

void MainWindow::RemoveFavorite(const std::string& url) {
  if (!favorites_) {
    return;
  }
  favorites_->Remove(url);
  PushBrowserState();
}

void MainWindow::MoveFavorite(int from, int to) {
  if (!favorites_) {
    return;
  }
  favorites_->Move(from, to);
  PushBrowserState();
}

void MainWindow::OnPageTitleChanged(int tab_id, const std::string& title) {
  Tab* tab = FindTab(tab_id);
  if (!tab) {
    return;
  }
  tab->title = title.empty() ? "Untitled" : title;
  PushBrowserState();
}

void MainWindow::OnPageUrlChanged(int tab_id, const std::string& url) {
  Tab* tab = FindTab(tab_id);
  if (!tab) {
    return;
  }
  tab->url = url;
  PushBrowserState();
}

// --- state push -----------------------------------------------------------

std::string MainWindow::BuildBrowserStateJson() const {
  const Tab* active = FindTab(active_tab_id_);
  std::ostringstream json;
  json << "{\"activeTabId\":" << active_tab_id_
       << ",\"sidebarOpen\":" << (sidebar_open_ ? "true" : "false")
       << ",\"incognito\":" << (options_.incognito ? "true" : "false")
       << ",\"canGoBack\":"
       << (active && active->can_go_back ? "true" : "false")
       << ",\"canGoForward\":"
       << (active && active->can_go_forward ? "true" : "false")
       << ",\"loading\":" << (active && active->loading ? "true" : "false")
       << ",\"address\":\"" << JsonEscape(active ? active->url : "") << "\""
       << ",\"favorites\":[";
  if (favorites_) {
    const std::vector<Favorite>& pinned = favorites_->items();
    for (size_t i = 0; i < pinned.size(); ++i) {
      if (i) {
        json << ',';
      }
      // The icon travels as a data: URL inside the payload the chrome already
      // receives, so displaying it needs no extra plumbing.
      const std::string icon =
          favicons_ ? favicons_->DataUrl(HostOf(pinned[i].url)) : std::string();
      json << "{\"url\":\"" << JsonEscape(pinned[i].url) << "\",\"title\":\""
           << JsonEscape(pinned[i].title) << "\",\"icon\":\""
           << JsonEscape(icon) << "\"}";
    }
  }
  json << "],\"tabs\":[";
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (i) {
      json << ',';
    }
    json << "{\"id\":" << tabs_[i].id << ",\"title\":\""
         << JsonEscape(tabs_[i].title) << "\",\"url\":\""
         << JsonEscape(tabs_[i].url) << "\",\"loading\":"
         << (tabs_[i].loading ? "true" : "false") << "}";
  }
  json << "]}";
  return json.str();
}

void MainWindow::PushBrowserState() {
  const std::string state = BuildBrowserStateJson();
  // Both surfaces get the same payload: the topbar draws the tab strip from
  // it, the sidebar its address field. One state, no divergence.
  for (size_t i = 0; i < static_cast<size_t>(SurfaceId::kCount); ++i) {
    if (!layers_[i].browser) {
      continue;
    }
    CefRefPtr<CefFrame> main_frame = layers_[i].browser->GetMainFrame();
    if (!main_frame) {
      continue;
    }
    const std::string js =
        "window.FrameShell && FrameShell.onBrowserState(" + state + ");";
    main_frame->ExecuteJavaScript(js, main_frame->GetURL(), 0);
  }
}

void MainWindow::PushPageShellMetrics() {
  // Frame's own pages anchor the shell field to the window, the same way the
  // chrome surfaces do, and derive everything they need from their own viewport
  // plus the layout constants — everything except this. Collapsing the sidebar
  // moves the page's origin, and no amount of CSS can see that happen.
  const int shell_x = SidebarWidthDip();

  for (Tab& tab : tabs_) {
    if (!tab.browser) {
      continue;
    }
    // frame:// ONLY. Running script in a site's page would be a privacy
    // problem and a compatibility one, and no site needs to know where Frame
    // put its sidebar.
    if (!frame::url::StartsWith(tab.url, "frame://")) {
      continue;
    }
    CefRefPtr<CefFrame> main_frame = tab.browser->GetMainFrame();
    if (!main_frame) {
      continue;
    }
    // An inline property on the root element, which outranks the stylesheet's
    // own fallback and feeds back into the calc() that derives the rest.
    std::ostringstream js;
    js << "document.documentElement.style.setProperty('--shell-x','" << shell_x
       << "px');document.documentElement.style.setProperty('--shell-y','"
       << layout::kTopbarHeight << "px');";
    main_frame->ExecuteJavaScript(js.str(), main_frame->GetURL(), 0);
  }
}

void MainWindow::PushShellMetrics() {
  // The shell gradient is anchored to the window, so every surface needs to be
  // told when the window's size or its own origin changes. Fetching this once
  // at startup is not enough: after a resize the gradient would still be laid
  // out for the old window and would visibly stop part-way down the surface.
  for (size_t i = 0; i < static_cast<size_t>(SurfaceId::kCount); ++i) {
    if (!layers_[i].browser) {
      continue;
    }
    CefRefPtr<CefFrame> main_frame = layers_[i].browser->GetMainFrame();
    if (!main_frame) {
      continue;
    }
    const CefRect bounds = SurfaceBoundsDip(static_cast<SurfaceId>(i));
    std::ostringstream js;
    js << "window.FrameShell && FrameShell.onShellMetrics({surfaceX:"
       << bounds.x << ",surfaceY:" << bounds.y
       << ",surfaceWidth:" << bounds.width
       << ",surfaceHeight:" << bounds.height
       << ",windowWidth:" << ClientWidthDip()
       << ",windowHeight:" << ClientHeightDip() << "});";
    main_frame->ExecuteJavaScript(js.str(), main_frame->GetURL(), 0);
  }
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
  // whole window: no caption, no frame.
  return 0;
}

LRESULT MainWindow::HitTest(POINT screen_point) {
  if (fullscreen_) {
    // No caption to drag by and no border to resize from. Without this the
    // window can still be dragged off the monitor by its invisible top strip.
    return HTCLIENT;
  }

  POINT raw = screen_point;
  ::ScreenToClient(hwnd_, &raw);

  // Hit testing happens entirely in DIPs: the layout constants are DIPs, and
  // so are the drag regions the surface reports from its own DOM. Converting
  // the pointer once here keeps every comparison below in one space.
  POINT p = {ToDip(raw.x), ToDip(raw.y)};
  const int client_width = ClientWidthDip();
  const int client_height = ClientHeightDip();

  const int border = layout::kResizeBorderThickness;

  if (!IsWindowMaximized()) {
    const bool top = p.y >= 0 && p.y < border;
    const bool bottom = p.y < client_height && p.y >= client_height - border;
    const bool left = p.x >= 0 && p.x < border;
    const bool right = p.x < client_width && p.x >= client_width - border;

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
    // HTMAXBUTTON is the only hit-test result Windows offers the Snap Layouts
    // flyout for, so this cannot be handled as an ordinary client click.
    if (layout::MaximizeButtonRect(client_width).Contains(p.x, p.y)) {
      return HTMAXBUTTON;
    }
    if (layout::MinimizeButtonRect(client_width).Contains(p.x, p.y) ||
        layout::CloseButtonRect(client_width).Contains(p.x, p.y)) {
      return HTCLIENT;
    }
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
  // Negative height: OnPaint delivers top-down, GDI assumes bottom-up.
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
  // clears the shell margins.
  RECT client = {0, 0, client_width_, client_height_};
  HBRUSH shell = ::CreateSolidBrush(kShellBackground);
  ::FillRect(hdc, &client, shell);
  ::DeleteObject(shell);

  // Only paint the viewport placeholder when no page covers it; a live page
  // owns those pixels itself.
  if (tabs_.empty() || !ActiveBrowser()) {
    const layout::ViewportRect dip = ViewportDip();
    const layout::ViewportRect viewport = {
        ToPhysical(dip.x), ToPhysical(dip.y), ToPhysical(dip.width),
        ToPhysical(dip.height), ToPhysical(dip.radius)};
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
  }

  // In fullscreen the surfaces keep their pixels but are simply not composited,
  // which is what makes coming back out instant.
  if (!fullscreen_) {
    PaintLayer(hdc, SurfaceId::kTopbar);
    PaintLayer(hdc, SurfaceId::kSidebar);
  }
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

  // The corner masks take their colour from these pixels, and the first layout
  // pass runs before the sidebar has ever painted — so without this the
  // top-left mask would keep the fallback shell colour, which is the black
  // wedge it exists to avoid, until something else forced a re-layout.
  //
  // Cheap to repeat: CornerMask only rebuilds a bitmap when that corner's
  // colour actually changed, and the sidebar repaints on interaction, not on a
  // clock.
  if (id == SurfaceId::kSidebar) {
    UpdateCornerMasks();
  }

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
  // Nothing is on screen to hit in fullscreen, so nothing may claim the
  // pointer — otherwise the invisible topbar still swallows clicks along the
  // top edge of a video.
  if (fullscreen_) {
    return false;
  }

  // Works in DIPs and returns DIP-local coordinates: that is the space the
  // surface lays out in, so it is the space CEF expects mouse events in.
  const int dip_x = ToDip(x);
  const int dip_y = ToDip(y);

  // Topbar first: it spans the full width and wins along the top edge.
  const SurfaceId order[] = {SurfaceId::kTopbar, SurfaceId::kSidebar};
  for (SurfaceId candidate : order) {
    const CefRect bounds = SurfaceBoundsDip(candidate);
    if (dip_x >= bounds.x && dip_x < bounds.x + bounds.width &&
        dip_y >= bounds.y && dip_y < bounds.y + bounds.height) {
      *id = candidate;
      *local_x = dip_x - bounds.x;
      *local_y = dip_y - bounds.y;
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

void MainWindow::FocusSurface(SurfaceId id) {
  focused_surface_ = id;
  for (size_t i = 0; i < static_cast<size_t>(SurfaceId::kCount); ++i) {
    if (layers_[i].browser) {
      layers_[i].browser->GetHost()->SetFocus(static_cast<size_t>(id) == i);
    }
  }
  if (id != SurfaceId::kCount) {
    // Key messages only reach this window if it holds the native focus, which
    // the page's child window may currently have.
    ::SetFocus(hwnd_);
  }
}

void MainWindow::ForwardMouseMove(int x, int y, WPARAM wparam) {
  SurfaceId id = SurfaceId::kTopbar;
  int local_x = 0;
  int local_y = 0;
  const bool inside = SurfaceAt(x, y, &id, &local_x, &local_y);

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
  // Clicking a surface gives it the keyboard, which is what makes the address
  // field typeable.
  if (!up && focused_surface_ != id) {
    FocusSurface(id);
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

bool MainWindow::TryNativeShortcut(WPARAM wparam) {
  // Only when neither the page nor a chrome surface owns the keyboard: both of
  // those reach ExecuteCommand through OnPreKeyEvent instead, and a chord
  // handled in two places fires twice.
  if (focused_surface_ != SurfaceId::kCount) {
    return false;
  }

  shortcuts::Chord chord;
  chord.key = static_cast<int>(wparam);
  chord.ctrl = ::GetKeyState(VK_CONTROL) < 0;
  chord.shift = ::GetKeyState(VK_SHIFT) < 0;
  chord.alt = ::GetKeyState(VK_MENU) < 0;

  const shortcuts::Command command = shortcuts::Match(chord);
  if (command == shortcuts::Command::kNone ||
      shortcuts::IsEditCommand(command)) {
    return false;
  }
  return ExecuteCommand(command);
}

void MainWindow::ForwardKeyEvent(UINT message, WPARAM wparam, LPARAM lparam) {
  if (focused_surface_ == SurfaceId::kCount) {
    return;
  }
  Layer& target = layer(focused_surface_);
  if (!target.browser) {
    return;
  }

  CefKeyEvent event;
  event.windows_key_code = static_cast<int>(wparam);
  event.native_key_code = static_cast<int>(lparam);
  event.is_system_key = message == WM_SYSCHAR || message == WM_SYSKEYDOWN ||
                        message == WM_SYSKEYUP;
  event.modifiers = KeyboardModifiers(wparam, lparam);

  if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) {
    // RAWKEYDOWN rather than KEYDOWN: CEF synthesises the KEYDOWN itself from
    // the WM_CHAR that follows, and sending both duplicates every keystroke.
    event.type = KEYEVENT_RAWKEYDOWN;
  } else if (message == WM_KEYUP || message == WM_SYSKEYUP) {
    event.type = KEYEVENT_KEYUP;
  } else {
    event.type = KEYEVENT_CHAR;
  }

  target.browser->GetHost()->SendKeyEvent(event);
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

    // The caption buttons are non-client as far as Windows is concerned, so
    // ordinary mouse messages never reach them.
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
          event.x = ToDip(p.x);
          event.y = ToDip(p.y);
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
        return 0;  // Do not let the system start its own caption drag.
      }
      break;

    case WM_NCLBUTTONUP:
      if (wparam == HTMAXBUTTON) {
        ToggleMaximize();
        return 0;
      }
      break;

    case WM_TIMER:
      if (wparam == kSidebarTimerId) {
        TickSidebarAnimation();
        return 0;
      }
      break;

    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT: {
      PAINTSTRUCT ps = {};
      HDC hdc = ::BeginPaint(hwnd, &ps);
      Paint(hdc);
      ::EndPaint(hwnd, &ps);
      return 0;
    }

    case WM_SIZE:
      if (wparam == SIZE_MINIMIZED) {
        // The corner masks are top-level windows, so they do not minimise with
        // their owner's client area — they have to be hidden explicitly or they
        // hang in mid-air over the desktop.
        corner_mask_.Hide();
        return 0;
      }
      client_width_ = LOWORD(lparam);
      client_height_ = HIWORD(lparam);
      NotifySurfacesResized();
      LayoutPages();
      PushShellMetrics();
      PushWindowState();
      return 0;

    case WM_MOVE:
      // Same reason: the masks are positioned in screen coordinates, so moving
      // the window has to drag them along with it.
      LayoutPages();
      return 0;

    case WM_DPICHANGED: {
      // Dragged to a monitor with different scaling, or the scaling changed
      // underneath us. Windows supplies the rectangle the window should take
      // at the new scale; not honouring it leaves the window the wrong
      // physical size on the new display.
      dpi_ = HIWORD(wparam) > 0 ? HIWORD(wparam) : 96;
      const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
      if (suggested) {
        ::SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                       suggested->right - suggested->left,
                       suggested->bottom - suggested->top,
                       SWP_NOZORDER | SWP_NOACTIVATE);
      }
      // Every surface has to re-render at the new scale, not just re-lay out:
      // an off-screen surface rasterises at the device scale it was told
      // about, so a stale one stays blurry.
      for (size_t i = 0; i < static_cast<size_t>(SurfaceId::kCount); ++i) {
        if (layers_[i].browser) {
          layers_[i].browser->GetHost()->NotifyScreenInfoChanged();
          layers_[i].browser->GetHost()->WasResized();
        }
      }
      LayoutPages();
      PushShellMetrics();
      ::InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }

    case WM_MOUSEMOVE: {
      if (!tracking_mouse_) {
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

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
      // Last of the three shortcut entry points, for the window that has the
      // native focus while neither the page nor a chrome surface holds the
      // keyboard — briefly true at startup, and after the page's child window
      // gives focus back. The other two are the OnPreKeyEvent overrides.
      if (TryNativeShortcut(wparam)) {
        return 0;
      }
      ForwardKeyEvent(message, wparam, lparam);
      return 0;

    case WM_KEYUP:
    case WM_CHAR:
    case WM_SYSKEYUP:
    case WM_SYSCHAR:
      ForwardKeyEvent(message, wparam, lparam);
      return 0;

    case WM_KILLFOCUS:
      // Focus went to the page's child window; the chrome must stop showing a
      // caret and stop claiming keyboard input.
      if (focused_surface_ != SurfaceId::kCount) {
        FocusSurface(SurfaceId::kCount);
      }
      break;

    case WM_CLOSE:
      for (size_t i = 0; i < static_cast<size_t>(SurfaceId::kCount); ++i) {
        if (layers_[i].browser) {
          layers_[i].browser->GetHost()->CloseBrowser(/*force_close=*/true);
        }
      }
      break;

    case WM_DESTROY:
      // The window list decides whether this was the last window and therefore
      // whether the application ends. Quitting the message loop from here
      // directly is what made a second window impossible.
      ::SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
      windows::OnWindowDestroyed(this);
      return 0;

    default:
      break;
  }
  return ::DefWindowProc(hwnd, message, wparam, lparam);
}

}  // namespace frame
