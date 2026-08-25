#ifndef FRAME_BROWSER_PAGE_CLIENT_H_
#define FRAME_BROWSER_PAGE_CLIENT_H_

#include <string>
#include <vector>

#include "include/cef_client.h"
#include "include/cef_display_handler.h"
#include "include/cef_keyboard_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"

namespace frame {

class MainWindow;

// CefClient for an actual web page.
//
// Unlike the chrome surfaces, page browsers are NOT off-screen rendered. They
// are real child windows, which is the right trade for page content: CEF owns
// their input, focus and compositing, so typing, scrolling, IME and shortcuts
// all work without being reimplemented. Off-screen rendering earns its keep on
// chrome, where we need the bitmap; here it would only cost us.
class PageClient : public CefClient,
                   public CefLifeSpanHandler,
                   public CefLoadHandler,
                   public CefDisplayHandler,
                   public CefKeyboardHandler {
 public:
  PageClient(MainWindow* window, int tab_id);

  // CefClient
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }

  // CefKeyboardHandler
  //
  // The page is a native child window, so its keys never reach Frame's own
  // message loop — this is the only place a shortcut pressed while browsing
  // can be seen at all.
  bool OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                     const CefKeyEvent& event,
                     CefEventHandle os_event,
                     bool* is_keyboard_shortcut) override;

  // CefLifeSpanHandler
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

  // CefLoadHandler
  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                            bool is_loading,
                            bool can_go_back,
                            bool can_go_forward) override;
  void OnLoadError(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   ErrorCode error_code,
                   const CefString& error_text,
                   const CefString& failed_url) override;

  // CefDisplayHandler
  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override;
  void OnAddressChange(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       const CefString& url) override;
  void OnFaviconURLChange(CefRefPtr<CefBrowser> browser,
                          const std::vector<CefString>& icon_urls) override;

  void Detach() { window_ = nullptr; }

 private:
  MainWindow* window_;  // Not owned; cleared by Detach() during teardown.
  const int tab_id_;

  IMPLEMENT_REFCOUNTING(PageClient);
  DISALLOW_COPY_AND_ASSIGN(PageClient);
};

}  // namespace frame

#endif  // FRAME_BROWSER_PAGE_CLIENT_H_
