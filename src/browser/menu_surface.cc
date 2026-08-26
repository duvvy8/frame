#include "browser/menu_surface.h"

#include <windowsx.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_render_handler.h"
#include "include/wrapper/cef_message_router.h"

namespace frame {
namespace {

// TWO classes, one implementation. The menu and the tooltip are both
// MenuSurface, and giving them the same window class made them
// indistinguishable from outside the process — which is a real problem for
// anything that has to find one of them, and the reason a test that looked up
// "the popup" started clicking on whichever happened to be created first.
// Two things that behave differently should not look identical to the platform.
const wchar_t kMenuClass[] = L"FrameMenuSurface";
const wchar_t kTooltipClass[] = L"FrameTooltipSurface";

// Bridge channels, named as constants for the same reason as everywhere else.
const char kQueryModel[] = "menu:model";
const char kCommandSize[] = "menu:size:";      // "<w>x<h>", DIPs
const char kCommandChoose[] = "menu:choose:";  // "<command id>"
const char kCommandDismiss[] = "menu:dismiss";

bool StartsWith(const std::string& value, const char* prefix) {
  return value.rfind(prefix, 0) == 0;
}

}  // namespace

// The menu's CefClient. Small enough to live here rather than in a header of
// its own — nothing outside this file has any business holding one.
class MenuClient : public CefClient,
                   public CefRenderHandler,
                   public CefLifeSpanHandler {
 public:
  explicit MenuClient(MenuSurface* surface) : surface_(surface) {
    CefMessageRouterConfig config;
    router_ = CefMessageRouterBrowserSide::Create(config);
    handler_.reset(new Handler(surface));
    router_->AddHandler(handler_.get(), /*first=*/false);
  }

  void Detach() {
    surface_ = nullptr;
    if (handler_) {
      handler_->Detach();
    }
  }

  CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }

  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override {
    return router_->OnProcessMessageReceived(browser, frame, source_process,
                                             message);
  }

  // CefRenderHandler
  void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override {
    rect.x = 0;
    rect.y = 0;
    rect.width = surface_ ? std::max(1, surface_->view_width_dip()) : 1;
    rect.height = surface_ ? std::max(1, surface_->view_height_dip()) : 1;
  }

  bool GetScreenInfo(CefRefPtr<CefBrowser> browser,
                     CefScreenInfo& screen_info) override {
    if (!surface_) {
      return false;
    }
    screen_info.device_scale_factor = surface_->device_scale();
    return true;
  }

  void OnPaint(CefRefPtr<CefBrowser> browser,
               PaintElementType type,
               const RectList& dirty_rects,
               const void* buffer,
               int width,
               int height) override {
    if (type == PET_VIEW && surface_) {
      surface_->OnPaint(buffer, width, height);
    }
  }

  // CefLifeSpanHandler
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    if (surface_) {
      surface_->OnBrowserCreated(browser);
    }
  }

  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    router_->OnBeforeClose(browser);
    if (surface_) {
      surface_->OnBrowserClosed();
    }
  }

 private:
  class Handler : public CefMessageRouterBrowserSide::Handler {
   public:
    explicit Handler(MenuSurface* surface) : surface_(surface) {}
    void Detach() { surface_ = nullptr; }

    bool OnQuery(CefRefPtr<CefBrowser> browser,
                 CefRefPtr<CefFrame> frame,
                 int64_t query_id,
                 const CefString& request,
                 bool persistent,
                 CefRefPtr<Callback> callback) override {
      if (!surface_) {
        return false;
      }
      const std::string name = request.ToString();

      if (name == kQueryModel) {
        callback->Success(surface_->TakePendingModel());
        return true;
      }
      if (StartsWith(name, kCommandSize)) {
        const std::string payload = name.substr(std::strlen(kCommandSize));
        const size_t split = payload.find('x');
        if (split != std::string::npos) {
          surface_->OnContentSized(std::atoi(payload.substr(0, split).c_str()),
                                   std::atoi(payload.substr(split + 1).c_str()));
        }
        callback->Success("ok");
        return true;
      }
      if (StartsWith(name, kCommandChoose)) {
        surface_->OnCommandChosen(name.substr(std::strlen(kCommandChoose)));
        callback->Success("ok");
        return true;
      }
      if (name == kCommandDismiss) {
        surface_->OnCommandChosen(std::string());
        callback->Success("ok");
        return true;
      }
      return false;
    }

   private:
    MenuSurface* surface_;
  };

  MenuSurface* surface_;
  CefRefPtr<CefMessageRouterBrowserSide> router_;
  std::unique_ptr<Handler> handler_;

  IMPLEMENT_REFCOUNTING(MenuClient);
  DISALLOW_COPY_AND_ASSIGN(MenuClient);
};

// ---------------------------------------------------------------------------

MenuSurface::MenuSurface() = default;

MenuSurface::~MenuSurface() {
  closing_ = true;
  closed_ = nullptr;
  if (client_) {
    client_->Detach();
  }
  // By now the window should already have closed this through CloseBrowser()
  // and waited for it. This is the backstop for a MenuSurface destroyed
  // without that — it cannot wait here, so it does the best it can.
  if (browser_) {
    browser_->GetHost()->CloseBrowser(/*force_close=*/true);
    browser_ = nullptr;
  }
  if (hwnd_) {
    ::DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

bool MenuSurface::Create(HWND owner,
                         HINSTANCE instance,
                         const char* page_url,
                         bool click_through) {
  page_url_ = page_url;
  click_through_ = click_through;

  // The class a click-through popup registers is the tooltip one; everything
  // else is a menu. Each is registered once.
  const wchar_t* class_name = click_through_ ? kTooltipClass : kMenuClass;
  static bool menu_registered = false;
  static bool tooltip_registered = false;
  bool& registered = click_through_ ? tooltip_registered : menu_registered;
  if (!registered) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &MenuSurface::WndProc;
    wc.hInstance = instance;
    wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = class_name;
    ::RegisterClassExW(&wc);
    registered = true;
  }

  owner_ = owner;

  // Owned popup, layered, never activated.
  //
  // NOACTIVATE is what stops the menu stealing focus from the page — a menu
  // that takes the caret and gives it back is a menu that loses the user's
  // place in a text field. It is deliberately NOT WS_EX_TRANSPARENT, unlike
  // the corner masks: this one exists to be clicked.
  DWORD ex = WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
  if (click_through_) {
    // A tooltip must never take a click. Without this it sits over the very
    // control it is describing and swallows the press that was meant for it.
    ex |= WS_EX_TRANSPARENT;
  }
  hwnd_ = ::CreateWindowExW(ex, class_name, L"", WS_POPUP, 0, 0, 1, 1, owner,
                            nullptr, instance, this);
  return hwnd_ != nullptr;
}

void MenuSurface::Open(const std::string& model_json,
                       const Context& context,
                       float device_scale) {
  if (!hwnd_) {
    return;
  }
  context_ = context;
  pending_model_ = model_json;
  device_scale_ = device_scale > 0.0f ? device_scale : 1.0f;
  closing_ = false;

  // Back to the provisional view size for every opening. A menu measured for
  // the last model would clip a longer label in this one.
  view_width_dip_ = 320;
  view_height_dip_ = 640;

  if (!browser_) {
    // creating_ matters. A right-click sends both a button-down and a
    // button-up through the topbar, and Blink can raise a contextmenu event
    // for the pair — so Open() arrives twice, the second time while the first
    // browser is still being created and browser_ is therefore still null.
    // Without this guard that made a SECOND menu browser rendering into the
    // same window, and the two then raced for a model only one of them could
    // have: whichever asked second got the empty fallback and drew a menu with
    // nothing in it.
    if (creating_) {
      return;  // Already on its way; the newer model above is what it will get.
    }
    creating_ = true;
    client_ = new MenuClient(this);
    CefWindowInfo window_info;
    window_info.SetAsWindowless(hwnd_);
    CefBrowserSettings settings;
    settings.windowless_frame_rate = 60;
    // Transparent, not a colour: the alpha in this bitmap becomes the window's
    // alpha, which is where the rounded corners and the shadow come from.
    settings.background_color = CefColorSetARGB(0, 0, 0, 0);
    CefBrowserHost::CreateBrowser(window_info, client_, page_url_, settings,
                                  nullptr, nullptr);
    return;  // Shown once it has loaded, measured and painted.
  }

  // Reused. Reloading is what re-runs the page's own open sequence — asking
  // for the model, measuring, reporting a size — so one code path serves both
  // the first opening and every one after it.
  browser_->GetHost()->WasHidden(false);
  browser_->GetHost()->WasResized();
  browser_->GetMainFrame()->LoadURL(page_url_);
}

void MenuSurface::OnBrowserCreated(CefRefPtr<CefBrowser> browser) {
  browser_ = browser;
  creating_ = false;
  // A browser that finished being created after the surface was told to close
  // is one nothing is going to use — and one a window waiting on it would wait
  // for forever, because nobody else is going to ask it to close.
  if (closing_ && !visible_) {
    browser_->GetHost()->CloseBrowser(/*force_close=*/true);
  }
}

void MenuSurface::CloseBrowser() {
  closing_ = true;
  visible_ = false;
  if (hwnd_) {
    ::ShowWindow(hwnd_, SW_HIDE);
  }
  if (browser_) {
    browser_->GetHost()->CloseBrowser(/*force_close=*/true);
  }
}

void MenuSurface::OnBrowserClosed() {
  browser_ = nullptr;
  creating_ = false;
  // The window may be waiting on this before it can finish closing.
  if (closed_) {
    closed_();
  }
}

std::string MenuSurface::TakePendingModel() {
  std::string model;
  model.swap(pending_model_);
  return model.empty() ? std::string("{\"items\":[]}") : model;
}

void MenuSurface::OnContentSized(int width_dip, int height_dip) {
  if (closing_ || width_dip <= 0 || height_dip <= 0) {
    return;
  }
  // Clamped so a page bug cannot ask for a menu the size of the desktop.
  view_width_dip_ = std::min(width_dip, 480);
  view_height_dip_ = std::min(height_dip, 900);
  if (browser_) {
    browser_->GetHost()->WasResized();
  }
}

void MenuSurface::PositionForAnchor(int width_px, int height_px) {
  // Flip rather than clamp when the menu will not fit below or to the right of
  // the anchor. Clamping slides the menu under the pointer, which puts an item
  // where the click was and makes the next click select something.
  HMONITOR monitor = ::MonitorFromPoint(context_.anchor, MONITOR_DEFAULTTONEAREST);
  MONITORINFO info = {sizeof(info)};
  ::GetMonitorInfo(monitor, &info);
  const RECT& work = info.rcWork;

  int x = context_.anchor.x;
  int y = context_.anchor.y;

  if (x + width_px > work.right) {
    x = context_.anchor.x - width_px;
  }
  if (y + height_px > work.bottom) {
    y = context_.anchor.y - height_px;
  }
  x = std::max(static_cast<int>(work.left), x);
  y = std::max(static_cast<int>(work.top), y);

  ::SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width_px, height_px,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void MenuSurface::OnPaint(const void* buffer, int width, int height) {
  if (!hwnd_ || closing_ || width <= 0 || height <= 0) {
    return;
  }

  // Straight to UpdateLayeredWindow. CEF hands back premultiplied BGRA, which
  // is exactly what AC_SRC_ALPHA expects, so the bitmap needs no conversion —
  // and the alpha in it is what gives the menu its antialiased corners and its
  // shadow instead of a rectangle pretending to have them.
  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;  // Top-down, as OnPaint delivers.
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;

  HDC screen = ::GetDC(nullptr);
  void* bits = nullptr;
  HBITMAP bitmap =
      ::CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!bitmap) {
    ::ReleaseDC(nullptr, screen);
    return;
  }
  memcpy(bits, buffer, static_cast<size_t>(width) * height * 4);

  HDC memory = ::CreateCompatibleDC(screen);
  HGDIOBJ previous = ::SelectObject(memory, bitmap);

  if (!visible_) {
    PositionForAnchor(width, height);
    visible_ = true;
  } else {
    ::SetWindowPos(hwnd_, nullptr, 0, 0, width, height,
                   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  }

  RECT bounds = {};
  ::GetWindowRect(hwnd_, &bounds);
  POINT position = {bounds.left, bounds.top};
  SIZE size = {width, height};
  POINT source = {0, 0};
  BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

  ::UpdateLayeredWindow(hwnd_, screen, &position, &size, memory, &source, 0,
                        &blend, ULW_ALPHA);

  ::SelectObject(memory, previous);
  ::DeleteDC(memory);
  ::DeleteObject(bitmap);
  ::ReleaseDC(nullptr, screen);
}

void MenuSurface::OnCommandChosen(const std::string& command_id) {
  // The context is copied before closing: the handler may open another menu,
  // and Close() is entitled to reset it.
  const Context context = context_;
  Close();
  if (choice_) {
    choice_(command_id, context);
  }
}

void MenuSurface::Close() {
  // NOT guarded on visible_.
  //
  // A menu can be dismissed before it has ever painted — the page failing to
  // reach the bridge does exactly that. Returning early there left closing_
  // false, so the first paint that arrived afterwards went on to show an empty
  // panel that nothing was going to fill.
  closing_ = true;
  visible_ = false;

  if (tracking_mouse_ && hwnd_) {
    tracking_mouse_ = false;
  }
  if (hwnd_) {
    ::ShowWindow(hwnd_, SW_HIDE);
  }
  // The browser is deliberately KEPT. Creating one costs a renderer process
  // and a page load, which is far too much to pay every time a menu opens —
  // and a menu that appears a beat after the click is a menu that feels
  // broken.
  //
  // WasHidden is what stops it costing anything while it waits: an off-screen
  // browser goes on producing frames at windowless_frame_rate otherwise, which
  // for a menu nobody can see is 60 wasted compositions a second.
  if (browser_) {
    browser_->GetHost()->WasHidden(true);
  }
}

bool MenuSurface::HandleKey(UINT message, WPARAM wparam, LPARAM lparam) {
  if (!visible_ || !browser_) {
    return false;
  }
  if (message == WM_KEYDOWN && wparam == VK_ESCAPE) {
    OnCommandChosen(std::string());
    return true;
  }
  // Arrows and Enter belong to the menu while it is up; anything else is left
  // alone so a stray key does not silently vanish.
  const bool navigational = wparam == VK_UP || wparam == VK_DOWN ||
                            wparam == VK_HOME || wparam == VK_END ||
                            wparam == VK_RETURN || wparam == VK_TAB;
  if (!navigational) {
    return false;
  }
  if (message != WM_KEYDOWN && message != WM_KEYUP) {
    return false;
  }

  CefKeyEvent event;
  event.windows_key_code = static_cast<int>(wparam);
  event.native_key_code = static_cast<int>(lparam);
  event.type = message == WM_KEYDOWN ? KEYEVENT_RAWKEYDOWN : KEYEVENT_KEYUP;
  browser_->GetHost()->SetFocus(true);
  browser_->GetHost()->SendKeyEvent(event);
  return true;
}

void MenuSurface::ForwardMouse(UINT message, WPARAM wparam, LPARAM lparam) {
  if (!browser_) {
    return;
  }
  const int px = GET_X_LPARAM(lparam);
  const int py = GET_Y_LPARAM(lparam);

  CefMouseEvent event;
  // The view is laid out in DIPs, so the pointer has to arrive in DIPs too.
  event.x = static_cast<int>(px / device_scale_);
  event.y = static_cast<int>(py / device_scale_);

  CefRefPtr<CefBrowserHost> host = browser_->GetHost();
  switch (message) {
    case WM_MOUSEMOVE:
      host->SendMouseMoveEvent(event, /*mouseLeave=*/false);
      break;
    case WM_MOUSELEAVE:
      host->SendMouseMoveEvent(event, /*mouseLeave=*/true);
      break;
    case WM_LBUTTONDOWN:
      host->SendMouseClickEvent(event, MBT_LEFT, /*mouseUp=*/false, 1);
      break;
    case WM_LBUTTONUP:
      host->SendMouseClickEvent(event, MBT_LEFT, /*mouseUp=*/true, 1);
      break;
    case WM_MOUSEWHEEL:
      host->SendMouseWheelEvent(event, 0, GET_WHEEL_DELTA_WPARAM(wparam));
      break;
    default:
      break;
  }
}

// static
LRESULT CALLBACK MenuSurface::WndProc(HWND hwnd,
                                      UINT message,
                                      WPARAM wparam,
                                      LPARAM lparam) {
  MenuSurface* self = nullptr;
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = static_cast<MenuSurface*>(create->lpCreateParams);
    ::SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self =
        reinterpret_cast<MenuSurface*>(::GetWindowLongPtr(hwnd, GWLP_USERDATA));
  }
  if (self) {
    return self->HandleMessage(hwnd, message, wparam, lparam);
  }
  return ::DefWindowProc(hwnd, message, wparam, lparam);
}

LRESULT MenuSurface::HandleMessage(HWND hwnd,
                                   UINT message,
                                   WPARAM wparam,
                                   LPARAM lparam) {
  switch (message) {
    case WM_MOUSEMOVE:
      if (!tracking_mouse_) {
        TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd, 0};
        ::TrackMouseEvent(&track);
        tracking_mouse_ = true;
      }
      ForwardMouse(message, wparam, lparam);
      return 0;

    case WM_MOUSELEAVE:
      tracking_mouse_ = false;
      ForwardMouse(message, wparam, lparam);
      return 0;

    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MOUSEWHEEL:
      ForwardMouse(message, wparam, lparam);
      return 0;

    // A right-click inside the menu dismisses it rather than opening another.
    case WM_RBUTTONDOWN:
      OnCommandChosen(std::string());
      return 0;

    // NCACTIVATE / MOUSEACTIVATE: never take activation, so the owner window
    // keeps its caret and its active-title appearance while the menu is up.
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;

    default:
      break;
  }
  return ::DefWindowProc(hwnd, message, wparam, lparam);
}

}  // namespace frame
