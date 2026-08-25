#include "browser/page_client.h"

#include "browser/main_window.h"

namespace frame {

PageClient::PageClient(MainWindow* window, int tab_id)
    : window_(window), tab_id_(tab_id) {}

void PageClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  if (window_) {
    window_->OnPageCreated(tab_id_, browser);
  }
}

void PageClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  if (window_) {
    window_->OnPageClosed(tab_id_);
  }
}

void PageClient::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                      bool is_loading,
                                      bool can_go_back,
                                      bool can_go_forward) {
  if (window_) {
    window_->OnPageLoadingChanged(tab_id_, is_loading, can_go_back,
                                  can_go_forward);
  }
}

void PageClient::OnLoadError(CefRefPtr<CefBrowser> browser,
                             CefRefPtr<CefFrame> frame,
                             ErrorCode error_code,
                             const CefString& error_text,
                             const CefString& failed_url) {
  // ERR_ABORTED fires for ordinary cancellations, including a navigation the
  // user replaced by starting another one. Reporting it would flash an error
  // for something that is not one.
  if (error_code == ERR_ABORTED || !frame->IsMain() || !window_) {
    return;
  }
  window_->OnPageLoadError(tab_id_, error_text.ToString(),
                           failed_url.ToString());
}

void PageClient::OnTitleChange(CefRefPtr<CefBrowser> browser,
                               const CefString& title) {
  if (window_) {
    window_->OnPageTitleChanged(tab_id_, title.ToString());
  }
}

void PageClient::OnAddressChange(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 const CefString& url) {
  // Sub-frame navigations must not rewrite the address bar.
  if (!frame->IsMain() || !window_) {
    return;
  }
  window_->OnPageUrlChanged(tab_id_, url.ToString());
}

}  // namespace frame
