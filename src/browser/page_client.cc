#include "browser/page_client.h"

#include "browser/main_window.h"
#include "shared/shortcuts.h"

namespace frame {

bool PageClient::OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                               const CefKeyEvent& event,
                               CefEventHandle os_event,
                               bool* is_keyboard_shortcut) {
  if (!window_) {
    return false;
  }

  // RAWKEYDOWN only. CEF delivers RAWKEYDOWN, then KEYDOWN, then CHAR for a
  // single press, so acting on more than one of them runs the command up to
  // three times — which for Ctrl+T means three new tabs.
  if (event.type != KEYEVENT_RAWKEYDOWN) {
    return false;
  }

  shortcuts::Chord chord;
  chord.key = event.windows_key_code;
  chord.ctrl = (event.modifiers & EVENTFLAG_CONTROL_DOWN) != 0;
  chord.shift = (event.modifiers & EVENTFLAG_SHIFT_DOWN) != 0;
  chord.alt = (event.modifiers & EVENTFLAG_ALT_DOWN) != 0;

  const shortcuts::Command command = shortcuts::Match(chord);

  // Copy, paste, undo and friends are left to Chromium. It already implements
  // them correctly for real page content — including inside text fields, cross
  // frame, and for the selection model the page actually has — and replacing
  // that with a CefFrame call would be a downgrade.
  if (command == shortcuts::Command::kNone ||
      shortcuts::IsEditCommand(command)) {
    return false;
  }

  // Returning true stops the key reaching web content, so a page cannot see —
  // or preventDefault() — the chord that closes its own tab.
  return window_->ExecuteCommand(command);
}

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

void PageClient::OnFaviconURLChange(CefRefPtr<CefBrowser> browser,
                                    const std::vector<CefString>& icon_urls) {
  // The first entry is the site's preferred icon.
  if (icon_urls.empty() || !window_) {
    return;
  }
  window_->OnPageFaviconChanged(tab_id_, icon_urls.front().ToString());
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
