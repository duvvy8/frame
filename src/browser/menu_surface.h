// Frame — the popup menu surface.
//
// A context menu has to draw OVER the page, and the page is the one thing in
// this window that cannot be drawn over. It is a real child browser window
// carrying WS_EX_NOREDIRECTIONBITMAP: Chromium composites it straight through
// DWM with no redirection surface, so a sibling child window painting on top
// of it reports success and produces nothing. The chrome surfaces cannot host
// the menu either — they are composited in the parent's WM_PAINT, which runs
// BENEATH child windows, and they are 32px and 168px anyway.
//
// So the menu is what the corner masks already are: an OWNED TOP-LEVEL LAYERED
// WINDOW, which DWM composites above its owner. That is the only layer above
// the page. It also means the menu is not clipped by the browser window, so it
// can hang off the bottom edge the way a real menu does.
//
// The content is a windowless CefBrowser rendering frame://menu. Its OnPaint
// bitmap goes straight to UpdateLayeredWindow, so the menu gets per-pixel
// alpha — real antialiased rounded corners and a real shadow, rather than a
// rectangle with corners painted on.
//
// The menu does not size itself from C++. Measuring text without a layout
// engine, to match what a layout engine will then do, is a guess that is wrong
// the first time a label changes; the page measures itself and reports back,
// and the window is shown only once it has.

#ifndef FRAME_BROWSER_MENU_SURFACE_H_
#define FRAME_BROWSER_MENU_SURFACE_H_

#include <windows.h>

#include <functional>
#include <string>

#include "include/cef_browser.h"

namespace frame {

class MenuClient;

class MenuSurface {
 public:
  // What the menu is for. The surface itself is generic; this is the context
  // the command handler needs to act on the chosen item.
  struct Context {
    // 0 for a menu that is not about a particular tab.
    int tab_id = 0;
    // Where the user right-clicked, in SCREEN pixels.
    POINT anchor = {0, 0};
  };

  // Called with the chosen command id, or an empty string if the menu was
  // dismissed without a choice.
  using ChoiceHandler = std::function<void(const std::string& command_id,
                                           const Context& context)>;

  MenuSurface();
  ~MenuSurface();

  MenuSurface(const MenuSurface&) = delete;
  MenuSurface& operator=(const MenuSurface&) = delete;

  // `page_url` is the frame:// page this popup renders. Parameterised because
  // a tooltip is the same machinery as a menu — an owned layered window, an
  // off-screen browser, a page that measures itself — differing only in what
  // it draws and whether it can be clicked. Duplicating the window plumbing to
  // get a tooltip would be two copies of the hardest part.
  //
  // `click_through` adds WS_EX_TRANSPARENT: the window is drawn but passes
  // every click to whatever is beneath it. Right for a tooltip, wrong for a
  // menu, and the two must not be confused — a click-through menu is a menu
  // you cannot use.
  bool Create(HWND owner,
              HINSTANCE instance,
              const char* page_url = "frame://menu",
              bool click_through = false);

  // Opens the menu at the anchor with the given model.
  //
  // `model_json` is the item list the page renders; see menu.html. It is
  // handed to the page rather than pushed as state because a menu's model is
  // decided once, at the moment it opens, and never changes while it is up.
  void Open(const std::string& model_json,
            const Context& context,
            float device_scale);

  void Close();
  bool visible() const { return visible_; }
  const Context& context() const { return context_; }

  // The menu keeps a browser alive between openings, so it is one of the
  // window's browsers and has to be counted as one when the window closes.
  //
  // Leaving it out was a real hazard: the window list quits the message loop
  // as soon as the last window is destroyed, and the menu's browser was only
  // being closed afterwards, in ~MenuSurface — asking CEF to tear down a
  // browser during a loop that is already ending, which is the one thing that
  // has to have finished before CefShutdown.
  bool has_browser() const { return browser_ != nullptr || creating_; }
  void CloseBrowser();

  void set_choice_handler(ChoiceHandler handler) {
    choice_ = std::move(handler);
  }

  // Called once the menu's browser has finished being destroyed, so a window
  // waiting on it can carry on closing.
  void set_closed_handler(std::function<void()> handler) {
    closed_ = std::move(handler);
  }

  // --- called by MenuClient -------------------------------------------------
  void OnBrowserCreated(CefRefPtr<CefBrowser> browser);
  void OnBrowserClosed();
  void OnPaint(const void* buffer, int width, int height);
  std::string TakePendingModel();
  // The page has laid itself out and knows how big it needs to be. This is
  // what actually puts the window on screen.
  void OnContentSized(int width_dip, int height_dip);
  void OnCommandChosen(const std::string& command_id);
  float device_scale() const { return device_scale_; }
  int view_width_dip() const { return view_width_dip_; }
  int view_height_dip() const { return view_height_dip_; }

  // Keyboard while the menu is up. Returns true if the menu consumed the key,
  // which is how Escape and the arrows stay out of the page.
  bool HandleKey(UINT message, WPARAM wparam, LPARAM lparam);

 private:
  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);

  void PositionForAnchor(int width_px, int height_px);
  void ForwardMouse(UINT message, WPARAM wparam, LPARAM lparam);

  HWND owner_ = nullptr;
  HWND hwnd_ = nullptr;
  std::string page_url_ = "frame://menu";
  bool click_through_ = false;
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<MenuClient> client_;

  bool visible_ = false;
  bool tracking_mouse_ = false;
  Context context_;
  ChoiceHandler choice_;
  std::function<void()> closed_;

  // Handed to the page when it asks. Cleared once taken, so a stale model can
  // never be served to a later opening.
  std::string pending_model_;

  // The view CEF renders into, in DIPs. Starts at a provisional size big
  // enough for the page to lay out in without wrapping, and is cut down to
  // what the page reports.
  int view_width_dip_ = 320;
  int view_height_dip_ = 640;
  float device_scale_ = 1.0f;

  // Set while the window is being taken down, so a paint arriving mid-close
  // does not put it back on screen.
  bool closing_ = false;

  // Set between asking CEF for the menu browser and its OnAfterCreated. A
  // right-click can reach Open() twice before the first browser exists, and
  // without this the second call creates another one. See Open().
  bool creating_ = false;
};

}  // namespace frame

#endif  // FRAME_BROWSER_MENU_SURFACE_H_
