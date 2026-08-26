#ifndef FRAME_BROWSER_PAGE_CLIENT_H_
#define FRAME_BROWSER_PAGE_CLIENT_H_

#include <string>
#include <vector>

#include <atomic>
#include <memory>

#include "browser/window_ref.h"
#include "include/cef_client.h"
#include "include/cef_display_handler.h"
#include "include/cef_keyboard_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_context_menu_handler.h"
#include "include/cef_download_handler.h"
#include "include/cef_find_handler.h"
#include "include/cef_request_handler.h"
#include "include/cef_resource_request_handler.h"
#include "include/wrapper/cef_message_router.h"

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
                   public CefKeyboardHandler,
                   public CefRequestHandler,
                   public CefResourceRequestHandler,
                   public CefDownloadHandler,
                   public CefContextMenuHandler,
                   public CefFindHandler {
 public:
  PageClient(CefRefPtr<WindowRef> window, int tab_id);

  // How many requests this tab has had blocked. Written on the IO thread,
  // read on the UI thread, so it is atomic rather than merely an int.
  std::size_t blocked_count() const {
    return blocked_count_.load(std::memory_order_relaxed);
  }

  // CefClient
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
  CefRefPtr<CefFindHandler> GetFindHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override {
    return this;
  }

  // The bridge for Frame's own pages.
  //
  // frame://settings, frame://history and frame://downloads are ordinary tabs,
  // so they are served by this client like any other page — but unlike any
  // other page they need to read and change browser state. The router is what
  // gives them a way to ask.
  //
  // SECURITY: the privileged half of the handler refuses anything whose frame
  // is not itself a frame:// document. Without that check, any page on the web
  // could call window.cefQuery('frame:settings:set:...') and rewrite the
  // browser's configuration. The check is on the FRAME's URL, not on a page's
  // claim about itself.
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;

  // CefRequestHandler — router bookkeeping. The message router requires both
  // of these or a query outstanding across a navigation is never cleaned up.
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                      CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefRequest> request,
                      bool user_gesture,
                      bool is_redirect) override;
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                 TerminationStatus status,
                                 int error_code,
                                 const CefString& error_string) override;

  // CefDownloadHandler
  //
  // Frame had none, which is why a download used to be declined outright. The
  // store behind these is what the downloads page reads.
  bool OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefDownloadItem> download_item,
                        const CefString& suggested_name,
                        CefRefPtr<CefBeforeDownloadCallback> callback) override;
  void OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefDownloadItem> download_item,
                         CefRefPtr<CefDownloadItemCallback> callback) override;

  // CefFindHandler
  //
  // Where "3 of 47" comes from. CEF reports find results asynchronously and
  // repeatedly as a search progresses, with `final_update` marking the last
  // one for a query.
  void OnFindResult(CefRefPtr<CefBrowser> browser,
                    int identifier,
                    int count,
                    const CefRect& selection_rect,
                    int active_match_ordinal,
                    bool final_update) override;

  // CefContextMenuHandler
  //
  // Chromium's own menu is suppressed on Frame's internal pages, where it
  // offers page actions that make no sense for browser UI. Real web content
  // keeps it: replacing a fully-featured page menu with a partial imitation
  // would be a downgrade, and the tab strip menu is Frame's own surface.
  void OnBeforeContextMenu(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefContextMenuParams> params,
                           CefRefPtr<CefMenuModel> model) override;

  // CefRequestHandler — IO THREAD.
  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request,
      bool is_navigation,
      bool is_download,
      const CefString& request_initiator,
      bool& disable_default_handling) override;

  // CefResourceRequestHandler — IO THREAD.
  //
  // This is where a tracker is stopped: before the request is issued, rather
  // than by hiding its result afterwards. Nothing is injected into the page to
  // achieve it, so pages carry no per-request script cost.
  cef_return_value_t OnBeforeResourceLoad(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request,
      CefRefPtr<CefCallback> callback) override;

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

  // THE reason closing a tab used to close the whole browser.
  //
  // A page browser is created with SetAsChild, so it is windowed, and CEF
  // documents exactly what the default does in that case: "returning false
  // from DoClose() will send the standard close notification to the browser's
  // top-level parent window (e.g. WM_CLOSE on Windows)". PageClient did not
  // override DoClose, so every tab that closed posted WM_CLOSE to Frame's main
  // window and took the window down with it — by the book, not by accident.
  //
  // That notification exists for applications whose window IS the browser. A
  // tab is not a window, so this returns true instead: Frame handles the close
  // itself and the top-level window is not to be told anything.
  //
  // Returning true is only half of it, and the half on its own is worse than
  // the bug. CEF's contract is that a client returning true is "still required
  // to complete the browser close ... by proceeding with window hierarchy
  // tear-down" — so DoClose destroys the browser's own child window here.
  // Without that the window stopped closing but the TAB stopped closing too,
  // left in the partially-closed state the documentation warns about.
  //
  // Window close is a separate decision, made in MainWindow::OnPageClosed,
  // which ends the window only when the LAST tab has gone.
  bool DoClose(CefRefPtr<CefBrowser> browser) override;

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

 private:
  // Null once the window is gone. Checked at every use rather than cached:
  // CEF holds the last reference to a CefClient and can call back into it
  // after the window it belonged to has been destroyed. See window_ref.h.
  MainWindow* window() const { return window_ref_->get(); }

  CefRefPtr<WindowRef> window_ref_;
  const int tab_id_;
  std::atomic<std::size_t> blocked_count_{0};

  // The frame:// bridge. One per tab, because the handler has to know which
  // tab is asking — a settings page open in one tab must not be able to act on
  // another one.
  CefRefPtr<CefMessageRouterBrowserSide> router_;
  std::unique_ptr<CefMessageRouterBrowserSide::Handler> query_handler_;

  IMPLEMENT_REFCOUNTING(PageClient);
  DISALLOW_COPY_AND_ASSIGN(PageClient);
};

}  // namespace frame

#endif  // FRAME_BROWSER_PAGE_CLIENT_H_
