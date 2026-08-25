#include "browser/main_window.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

#include "browser/page_client.h"
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

const COLORREF kShellBackground = RGB(0x0b, 0x0b, 0x0d);
const COLORREF kViewportPlaceholder = RGB(0x00, 0x00, 0x00);

// A new tab opens on Frame's own page, not a blank one.
const char kNewTabPage[] = "frame://newtab";
const char kBlankPage[] = "about:blank";

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

std::string Trim(const std::string& value) {
  const size_t first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return std::string();
  }
  const size_t last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

bool StartsWith(const std::string& value, const char* prefix) {
  return value.rfind(prefix, 0) == 0;
}

std::string UrlEncode(const std::string& value) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char ch : value) {
    if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      out.push_back(static_cast<char>(ch));
    } else if (ch == ' ') {
      out.push_back('+');
    } else {
      out.push_back('%');
      out.push_back(hex[ch >> 4]);
      out.push_back(hex[ch & 0x0F]);
    }
  }
  return out;
}

// Decides whether omnibox input is a place to go or something to search for.
// Deliberately simple for now; the bangs and search-shortcut system replaces
// this wholesale in a later step.
std::string NormalizeUrl(const std::string& raw) {
  const std::string input = Trim(raw);
  if (input.empty()) {
    return kBlankPage;
  }
  if (StartsWith(input, "http://") || StartsWith(input, "https://") ||
      StartsWith(input, "file://") || StartsWith(input, "about:") ||
      StartsWith(input, "data:") || StartsWith(input, "chrome:") ||
      StartsWith(input, "frame:")) {
    return input;
  }
  if (input == "localhost" || StartsWith(input, "localhost:")) {
    return "http://" + input;
  }
  // A dot with no whitespace reads as a hostname; anything else is a query.
  const bool has_space = input.find(' ') != std::string::npos;
  const size_t dot = input.find('.');
  if (!has_space && dot != std::string::npos && dot > 0 &&
      dot + 1 < input.size()) {
    return "https://" + input;
  }
  return "https://www.google.com/search?q=" + UrlEncode(input);
}

// Minimal JSON string escaping. Page titles are attacker-influenced content,
// so this has to be correct rather than convenient: an unescaped quote in a
// title would break out of the string and into the script we execute.
std::string JsonEscape(const std::string& value) {
  std::ostringstream out;
  for (unsigned char ch : value) {
    switch (ch) {
      case '"':
        out << "\\\"";
        break;
      case '\\':
        out << "\\\\";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        if (ch < 0x20) {
          out << "\\u" << std::hex << std::setfill('0') << std::setw(4)
              << static_cast<int>(ch) << std::dec;
        } else {
          out << static_cast<char>(ch);
        }
    }
  }
  return out.str();
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

  ApplyRoundedCorners();
  corner_mask_.Create(hwnd_, instance);

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

CefRect MainWindow::SurfaceBounds(SurfaceId id) const {
  switch (id) {
    case SurfaceId::kTopbar:
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

void MainWindow::ToggleSidebar() {
  sidebar_open_ = !sidebar_open_;
  // The sidebar's own width changed, and the viewport moved with it.
  NotifySurfacesResized();
  PushShellMetrics();
  LayoutPages();
  ::InvalidateRect(hwnd_, nullptr, FALSE);
  PushBrowserState();
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
  tab.url = url.empty() ? kNewTabPage : url;
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

  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(255, 0, 0, 0);

  CefRefPtr<PageClient> client(new PageClient(this, tab.id));
  CefBrowserHost::CreateBrowser(window_info, client, NormalizeUrl(tab.url),
                                settings, nullptr, nullptr);

  PushBrowserState();
  return tab.id;
}

void MainWindow::CloseTab(int tab_id) {
  Tab* tab = FindTab(tab_id);
  if (!tab) {
    return;
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
  const layout::ViewportRect viewport = layout::ViewportBounds(
      {static_cast<double>(client_width_), static_cast<double>(client_height_),
       sidebar_open_, /*bookmarks_visible=*/false});

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
  if (ActiveBrowser()) {
    corner_mask_.Layout(viewport, kShellBackground);
    if (Tab* active = ActiveTab()) {
      if (active->browser) {
        corner_mask_.RaiseAbove(active->browser->GetHost()->GetWindowHandle());
      }
    }
  } else {
    corner_mask_.Hide();
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

void MainWindow::StopLoad() {
  if (CefRefPtr<CefBrowser> browser = ActiveBrowser()) {
    browser->StopLoad();
  }
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
  PushBrowserState();
}

void MainWindow::OnPageLoadError(int tab_id,
                                 const std::string& error_text,
                                 const std::string& failed_url) {
  Tab* tab = FindTab(tab_id);
  if (!tab) {
    return;
  }
  tab->title = "Unreachable";
  tab->loading = false;
  // A proper frame://unreachable page, with the Wayback fallback, comes with
  // the internal pages step. Until then the failure at least surfaces.
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
       << ",\"canGoBack\":"
       << (active && active->can_go_back ? "true" : "false")
       << ",\"canGoForward\":"
       << (active && active->can_go_forward ? "true" : "false")
       << ",\"loading\":" << (active && active->loading ? "true" : "false")
       << ",\"address\":\"" << JsonEscape(active ? active->url : "") << "\""
       << ",\"tabs\":[";
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
    const CefRect bounds = SurfaceBounds(static_cast<SurfaceId>(i));
    std::ostringstream js;
    js << "window.FrameShell && FrameShell.onShellMetrics({surfaceX:"
       << bounds.x << ",surfaceY:" << bounds.y
       << ",surfaceWidth:" << bounds.width
       << ",surfaceHeight:" << bounds.height
       << ",windowWidth:" << client_width_
       << ",windowHeight:" << client_height_ << "});";
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
  POINT p = screen_point;
  ::ScreenToClient(hwnd_, &p);

  const int border = layout::kResizeBorderThickness;

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
    // HTMAXBUTTON is the only hit-test result Windows offers the Snap Layouts
    // flyout for, so this cannot be handled as an ordinary client click.
    if (layout::MaximizeButtonRect(client_width_).Contains(p.x, p.y)) {
      return HTMAXBUTTON;
    }
    if (layout::MinimizeButtonRect(client_width_).Contains(p.x, p.y) ||
        layout::CloseButtonRect(client_width_).Contains(p.x, p.y)) {
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
    const layout::ViewportRect viewport = layout::ViewportBounds(
        {static_cast<double>(client_width_),
         static_cast<double>(client_height_), sidebar_open_,
         /*bookmarks_visible=*/false});
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
        return 0;  // Do not let the system start its own caption drag.
      }
      break;

    case WM_NCLBUTTONUP:
      if (wparam == HTMAXBUTTON) {
        ToggleMaximize();
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
    case WM_KEYUP:
    case WM_CHAR:
    case WM_SYSKEYDOWN:
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
      CefQuitMessageLoop();
      return 0;

    default:
      break;
  }
  return ::DefWindowProc(hwnd, message, wparam, lparam);
}

}  // namespace frame
