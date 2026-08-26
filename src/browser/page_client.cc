#include "browser/page_client.h"

#include <windows.h>
#include <shellapi.h>

#include <cstdlib>
#include <cstring>
#include <sstream>

#include "browser/browsing_data.h"
#include "browser/content_filter.h"
#include "browser/main_window.h"
#include "browser/window_list.h"
#include "shared/shortcuts.h"
#include "shared/sleep_policy.h"
#include "shared/tracker_filter.h"

namespace frame {
namespace {

using frame::url::JsonEscape;

// Channels the page side of the bridge answers. Constants for the same reason
// as every other channel name in this codebase.
const char kProbeReply[] = "tab:sleepprobe:";     // "<audible>:<dirty>"

const char kHistoryList[] = "frame:history:list:";    // "<query>"
const char kHistoryRemove[] = "frame:history:remove:";
const char kHistoryClear[] = "frame:history:clear:";  // "<sinceMs>"

const char kDownloadsList[] = "frame:downloads:list";
const char kDownloadsOpen[] = "frame:downloads:open:";
const char kDownloadsReveal[] = "frame:downloads:reveal:";
const char kDownloadsForget[] = "frame:downloads:forget:";
const char kDownloadsClear[] = "frame:downloads:clear";

const char kSettingsGet[] = "frame:settings:get";
const char kSettingsSet[] = "frame:settings:set:";  // "<key>=<value>"

const char kFavoritesList[] = "frame:favorites:list";
const char kFavoritesAdd[] = "frame:favorites:add:";     // "<url>\t<title>"
const char kFavoritesRemove[] = "frame:favorites:remove:";

const char kOpenUrl[] = "frame:open:";  // "<url>"

bool StartsWith(const std::string& value, const char* prefix) {
  return value.rfind(prefix, 0) == 0;
}

// THE privilege boundary.
//
// Everything above this line is browser state that a web page must never be
// able to read or change. The check is on the FRAME's own URL, which the
// renderer cannot forge — not on anything the page says about itself, and not
// on the tab's recorded URL, which is only what Frame last heard.
bool IsPrivilegedFrame(CefRefPtr<CefFrame> frame) {
  if (!frame) {
    return false;
  }
  const std::string url = frame->GetURL().ToString();
  return frame::url::StartsWith(url, "frame://");
}

// Answers queries from Frame's own pages, plus the sleep probe from any page.
class PageQueryHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  PageQueryHandler(CefRefPtr<WindowRef> window, int tab_id)
      : window_ref_(window), tab_id_(tab_id) {}

  bool OnQuery(CefRefPtr<CefBrowser> browser,
               CefRefPtr<CefFrame> frame,
               int64_t query_id,
               const CefString& request,
               bool persistent,
               CefRefPtr<Callback> callback) override {
    const std::string name = request.ToString();
    MainWindow* window = window_ref_->get();

    // --- unprivileged: the sleep probe ------------------------------------
    //
    // Answered for ANY page, because it is the page reporting on itself. It
    // carries no tab id: this handler already knows which tab it belongs to,
    // so a page cannot answer on another one's behalf. The worst it can do is
    // lie about itself and stay awake, which it could equally do by playing a
    // silent sound.
    if (StartsWith(name, kProbeReply)) {
      const std::string payload = name.substr(std::strlen(kProbeReply));
      const size_t split = payload.find(':');
      if (split != std::string::npos && window) {
        window->OnTabSleepProbe(tab_id_, payload.substr(0, split) == "1",
                                payload.substr(split + 1) == "1");
      }
      callback->Success("ok");
      return true;
    }

    // --- everything below is privileged -----------------------------------
    if (!StartsWith(name, "frame:")) {
      return false;
    }
    if (!IsPrivilegedFrame(frame)) {
      // Refused loudly rather than ignored. A page reaching for this is either
      // a bug in Frame's own UI or an attempt on it, and both are worth seeing.
      callback->Failure(403, "frame:// pages only");
      return true;
    }

    if (StartsWith(name, kHistoryList)) {
      callback->Success(HistoryJson(name.substr(std::strlen(kHistoryList))));
      return true;
    }
    if (StartsWith(name, kHistoryRemove)) {
      History().Remove(name.substr(std::strlen(kHistoryRemove)));
      callback->Success("ok");
      return true;
    }
    if (StartsWith(name, kHistoryClear)) {
      const std::string since = name.substr(std::strlen(kHistoryClear));
      callback->Success(std::to_string(
          History().ClearSince(since.empty() ? 0 : std::atoll(since.c_str()))));
      return true;
    }

    if (name == kDownloadsList) {
      callback->Success(DownloadsJson());
      return true;
    }
    if (StartsWith(name, kDownloadsOpen)) {
      Reveal(name.substr(std::strlen(kDownloadsOpen)), /*select=*/false);
      callback->Success("ok");
      return true;
    }
    if (StartsWith(name, kDownloadsReveal)) {
      Reveal(name.substr(std::strlen(kDownloadsReveal)), /*select=*/true);
      callback->Success("ok");
      return true;
    }
    if (StartsWith(name, kDownloadsForget)) {
      Downloads().Remove(name.substr(std::strlen(kDownloadsForget)));
      callback->Success("ok");
      return true;
    }
    if (name == kDownloadsClear) {
      Downloads().ClearAll();
      callback->Success("ok");
      return true;
    }

    if (name == kSettingsGet) {
      callback->Success(SettingsJson(window));
      return true;
    }
    if (StartsWith(name, kSettingsSet)) {
      const std::string payload = name.substr(std::strlen(kSettingsSet));
      const size_t equals = payload.find('=');
      if (equals != std::string::npos) {
        ApplySetting(payload.substr(0, equals), payload.substr(equals + 1));
      }
      callback->Success(SettingsJson(window));
      return true;
    }

    if (name == kFavoritesList) {
      callback->Success(FavoritesJson(window));
      return true;
    }
    if (StartsWith(name, kFavoritesAdd)) {
      const std::string payload = name.substr(std::strlen(kFavoritesAdd));
      const size_t tab = payload.find('\t');
      if (window) {
        window->AddFavorite(
            tab == std::string::npos ? payload : payload.substr(0, tab),
            tab == std::string::npos ? std::string() : payload.substr(tab + 1));
      }
      callback->Success(FavoritesJson(window));
      return true;
    }
    if (StartsWith(name, kFavoritesRemove)) {
      if (window) {
        window->RemoveFavorite(name.substr(std::strlen(kFavoritesRemove)));
      }
      callback->Success(FavoritesJson(window));
      return true;
    }

    if (StartsWith(name, kOpenUrl)) {
      if (window) {
        window->Navigate(name.substr(std::strlen(kOpenUrl)));
      }
      callback->Success("ok");
      return true;
    }

    return false;
  }

 private:
  static std::string HistoryJson(const std::string& query) {
    const std::vector<HistoryEntry> entries = History().Search(query, 500);
    std::ostringstream json;
    json << "{\"total\":" << History().size() << ",\"entries\":[";
    for (size_t i = 0; i < entries.size(); ++i) {
      if (i) {
        json << ',';
      }
      json << "{\"url\":\"" << JsonEscape(entries[i].url) << "\",\"title\":\""
           << JsonEscape(entries[i].title) << "\",\"visitedAt\":"
           << entries[i].visited_at_ms << ",\"visits\":"
           << entries[i].visit_count << '}';
    }
    json << "]}";
    return json.str();
  }

  static std::string DownloadsJson() {
    const std::vector<DownloadRecord>& items = Downloads().items();
    std::ostringstream json;
    json << "{\"items\":[";
    for (size_t i = 0; i < items.size(); ++i) {
      if (i) {
        json << ',';
      }
      json << "{\"url\":\"" << JsonEscape(items[i].url) << "\",\"path\":\""
           << JsonEscape(items[i].path) << "\",\"filename\":\""
           << JsonEscape(items[i].filename) << "\",\"state\":\""
           << JsonEscape(items[i].state) << "\",\"received\":"
           << items[i].received_bytes << ",\"total\":" << items[i].total_bytes
           << ",\"startedAt\":" << items[i].started_at_ms << '}';
    }
    json << "]}";
    return json.str();
  }

  static std::string FavoritesJson(MainWindow* window) {
    // Straight through the window's own store, so the bookmarks page and the
    // sidebar are reading ONE list. A second copy here is how the two end up
    // disagreeing about what is pinned.
    //
    // A page can outlive its window during teardown; an empty list is the
    // honest answer then, rather than a stale one.
    return window ? window->FavoritesJson() : std::string("{\"items\":[]}");
  }

  static std::string SettingsJson(MainWindow* window) {
    // The stored values PLUS what the browser is actually doing with them. A
    // settings page that only echoes the file cannot show that a setting
    // failed to take effect.
    std::ostringstream json;
    json << "{\"values\":" << Settings().ToJson() << ",\"effective\":{"
         << "\"sleepEnabled\":"
         << (window && window->sleep_enabled() ? "true" : "false")
         << ",\"sleepIdleMinutes\":"
         << (window ? window->sleep_idle_ms() / 60000 : 0)
         << ",\"historyEnabled\":"
         << (window && window->history_enabled() ? "true" : "false")
         << ",\"trackersEnabled\":"
         << (content_filter::enabled() ? "true" : "false")
         << ",\"blockedSoFar\":" << content_filter::total_blocked()
         << ",\"incognito\":"
         << (window && window->incognito() ? "true" : "false") << "}}";
    return json.str();
  }

  // The one place a setting turns into behaviour.
  //
  // Writing the file and changing what the browser does are deliberately the
  // same call. Splitting them is how a settings page ends up showing a toggle
  // that persists but controls nothing, which is exactly what this pass exists
  // to remove.
  static void ApplySetting(const std::string& key, const std::string& value) {
    Settings().SetString(key, value);
    const bool on = value == "1" || value == "true";

    // Applied to EVERY window, not just the one the page is open in. Settings
    // are process-wide; changing one in a second window and leaving the first
    // on the old value is a setting that is half applied, which is worse than
    // one that is not applied at all.
    if (key == "sleep.enabled") {
      windows::ForEach([on](MainWindow* w) { w->SetSleepEnabled(on); });
    } else if (key == "sleep.idleMinutes") {
      const unsigned long long ms =
          static_cast<unsigned long long>(
              std::max(1LL, std::atoll(value.c_str()))) * 60ULL * 1000ULL;
      windows::ForEach([ms](MainWindow* w) { w->SetSleepIdleMs(ms); });
    } else if (key == "history.enabled") {
      windows::ForEach([on](MainWindow* w) { w->SetHistoryEnabled(on); });
    } else if (key == "trackers.enabled") {
      // Process-wide by nature: one filter engine serves every window.
      content_filter::set_enabled(on);
    }
  }

  // Opens a downloaded file, or shows it in Explorer.
  //
  // ShellExecute on a path the browser itself wrote, never on a URL and never
  // on anything the page supplied — the path is looked up in the download
  // store first, so a page cannot ask Frame to launch an arbitrary file.
  static void Reveal(const std::string& path, bool select) {
    bool known = false;
    for (const DownloadRecord& item : Downloads().items()) {
      if (item.path == path && item.state == "complete") {
        known = true;
        break;
      }
    }
    if (!known) {
      return;
    }
    const std::wstring wide = CefString(path).ToWString();
    if (select) {
      const std::wstring args = L"/select,\"" + wide + L"\"";
      ::ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr,
                      SW_SHOWNORMAL);
    } else {
      ::ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr,
                      SW_SHOWNORMAL);
    }
  }

  CefRefPtr<WindowRef> window_ref_;
  const int tab_id_;
};

}  // namespace

CefRefPtr<CefResourceRequestHandler> PageClient::GetResourceRequestHandler(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    bool is_navigation,
    bool is_download,
    const CefString& request_initiator,
    bool& disable_default_handling) {
  // Navigations are never filtered. A blocking rule exists to stop a page
  // fetching a tracker, not to stop someone typing a URL and going there —
  // and a list that can silently refuse top-level navigation is a list that
  // can make the browser look broken with no way to tell why.
  if (is_navigation || is_download) {
    return nullptr;
  }
  return this;
}

cef_return_value_t PageClient::OnBeforeResourceLoad(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    CefRefPtr<CefCallback> callback) {
  // IO thread. The engine is immutable after startup, so this needs no lock;
  // see the comment in content_filter.h.
  const std::string url = request->GetURL().ToString();

  // Frame's own pages are never filtered, whatever a list says about them.
  if (frame::url::StartsWith(url, "frame://")) {
    return RV_CONTINUE;
  }

  // The document doing the asking, for first- vs third-party. GetFirstPartyURL
  // is what CEF fills in for the top-level document of the request.
  std::string document = request->GetFirstPartyForCookies().ToString();
  if (document.empty() && frame) {
    document = frame->GetURL().ToString();
  }
  const std::string document_host = filter::Engine::HostOfUrl(document);

  // The switch is checked BEFORE the engine, and it is the cheaper test: with
  // blocking off, a request costs one relaxed atomic load rather than a walk
  // of the rule set.
  if (!content_filter::enabled() ||
      !content_filter::Get().ShouldBlock(url, document_host)) {
    return RV_CONTINUE;
  }

  blocked_count_.fetch_add(1, std::memory_order_relaxed);
  content_filter::note_blocked();
  return RV_CANCEL;
}

bool PageClient::OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                               const CefKeyEvent& event,
                               CefEventHandle os_event,
                               bool* is_keyboard_shortcut) {
  if (!window()) {
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
  return window()->ExecuteCommand(command);
}

PageClient::PageClient(CefRefPtr<WindowRef> window, int tab_id)
    : window_ref_(window), tab_id_(tab_id) {
  // Same router config as the chrome surfaces: window.cefQuery. Frame's own
  // pages then use the identical shell.js bridge the surfaces do, rather than
  // a second mechanism that would drift from it.
  CefMessageRouterConfig config;
  router_ = CefMessageRouterBrowserSide::Create(config);
  query_handler_.reset(new PageQueryHandler(window, tab_id));
  router_->AddHandler(query_handler_.get(), /*first=*/false);
}

bool PageClient::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                          CefRefPtr<CefFrame> frame,
                                          CefProcessId source_process,
                                          CefRefPtr<CefProcessMessage> message) {
  return router_->OnProcessMessageReceived(browser, frame, source_process,
                                           message);
}

bool PageClient::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefRefPtr<CefRequest> request,
                                bool user_gesture,
                                bool is_redirect) {
  // Router bookkeeping, not a navigation policy. A query left outstanding
  // across a navigation would otherwise never be cleaned up.
  router_->OnBeforeBrowse(browser, frame);
  return false;
}

void PageClient::OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                           TerminationStatus status,
                                           int error_code,
                                           const CefString& error_string) {
  router_->OnRenderProcessTerminated(browser);
}

// --- downloads ------------------------------------------------------------

bool PageClient::OnBeforeDownload(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefDownloadItem> download_item,
    const CefString& suggested_name,
    CefRefPtr<CefBeforeDownloadCallback> callback) {
  if (!callback) {
    return false;
  }
  // show_dialog false, with an empty path: CEF then uses the platform's
  // default download directory and the site's suggested name. A dialog on
  // every download is the setting a browser should offer, not the behaviour it
  // should impose — and Frame has nowhere yet to put a "where do downloads go"
  // control, so the platform default is the honest choice rather than a
  // guessed one.
  //
  // Returning true is what tells CEF the callback WILL be run, which is the
  // difference between a download starting and one being silently declined —
  // the behaviour the downloads page used to have to apologise for.
  callback->Continue(CefString(), /*show_dialog=*/false);
  return true;
}

void PageClient::OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefDownloadItem> download_item,
                                   CefRefPtr<CefDownloadItemCallback> callback) {
  if (!download_item || !download_item->IsValid()) {
    return;
  }

  DownloadRecord record;
  record.id = download_item->GetId();
  record.url = download_item->GetURL().ToString();
  record.path = download_item->GetFullPath().ToString();
  record.filename = download_item->GetSuggestedFileName().ToString();
  if (record.filename.empty() && !record.path.empty()) {
    const size_t slash = record.path.find_last_of("\\/");
    record.filename =
        slash == std::string::npos ? record.path : record.path.substr(slash + 1);
  }
  record.received_bytes = download_item->GetReceivedBytes();
  record.total_bytes = download_item->GetTotalBytes();
  record.started_at_ms = WallClockMs();

  if (download_item->IsComplete()) {
    record.state = "complete";
  } else if (download_item->IsCanceled()) {
    record.state = "cancelled";
  } else if (download_item->IsInProgress()) {
    record.state = "in-progress";
  } else {
    record.state = "interrupted";
  }

  // A record with no path yet is a download CEF has not decided a location for;
  // writing it would put a row with no file behind it in the list.
  if (record.path.empty() && record.state != "cancelled") {
    return;
  }
  Downloads().Upsert(record);
}

void PageClient::OnFindResult(CefRefPtr<CefBrowser> browser,
                              int identifier,
                              int count,
                              const CefRect& selection_rect,
                              int active_match_ordinal,
                              bool final_update) {
  if (window()) {
    window()->OnFindResult(tab_id_, count, active_match_ordinal, final_update);
  }
}

void PageClient::OnBeforeContextMenu(CefRefPtr<CefBrowser> browser,
                                     CefRefPtr<CefFrame> frame,
                                     CefRefPtr<CefContextMenuParams> params,
                                     CefRefPtr<CefMenuModel> model) {
  // Frame's own pages are chrome, and Chromium's page menu offers Back,
  // Reload, View source and Inspect on them — actions that either make no
  // sense there or expose the browser's internals as if they were a site.
  //
  // Real web content keeps the full menu. Replacing it with an imitation would
  // cost spellcheck, editing commands, image and link actions, and the
  // selection model the page actually has — a straight downgrade, and not what
  // the tab-strip menu was asked for.
  if (frame && IsPrivilegedFrame(frame) && model) {
    model->Clear();
  }
}

void PageClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  if (window()) {
    window()->OnPageCreated(tab_id_, browser);
  }
}

bool PageClient::DoClose(CefRefPtr<CefBrowser> browser) {
  // See the contract quoted in the header. Destroying the browser's own child
  // window IS the tear-down CEF is waiting for; OnBeforeClose follows from it.
  //
  // GetWindowHandle() is CEF's window, not Frame's — SetAsChild made ours the
  // parent and CEF created its own child inside it. Destroying that child
  // closes exactly this tab and touches nothing else, whereas the default
  // behaviour reached past it to the top-level ancestor.
  if (browser) {
    if (HWND host = browser->GetHost()->GetWindowHandle()) {
      ::DestroyWindow(host);
    }
  }
  return true;
}

void PageClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  // Before the window is told: the router has to release any query still
  // outstanding for this browser, and OnPageClosed can start a teardown that
  // drops the last reference to this client.
  router_->OnBeforeClose(browser);
  if (window()) {
    window()->OnPageClosed(tab_id_);
  }
}

void PageClient::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                      bool is_loading,
                                      bool can_go_back,
                                      bool can_go_forward) {
  if (window()) {
    window()->OnPageLoadingChanged(tab_id_, is_loading, can_go_back,
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
  if (error_code == ERR_ABORTED || !frame->IsMain() || !window()) {
    return;
  }
  window()->OnPageLoadError(tab_id_, error_text.ToString(),
                           failed_url.ToString());
}

void PageClient::OnTitleChange(CefRefPtr<CefBrowser> browser,
                               const CefString& title) {
  if (window()) {
    window()->OnPageTitleChanged(tab_id_, title.ToString());
  }
}

void PageClient::OnFaviconURLChange(CefRefPtr<CefBrowser> browser,
                                    const std::vector<CefString>& icon_urls) {
  // The first entry is the site's preferred icon.
  if (icon_urls.empty() || !window()) {
    return;
  }
  window()->OnPageFaviconChanged(tab_id_, icon_urls.front().ToString());
}

void PageClient::OnAddressChange(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 const CefString& url) {
  // Sub-frame navigations must not rewrite the address bar.
  if (!frame->IsMain() || !window()) {
    return;
  }
  window()->OnPageUrlChanged(tab_id_, url.ToString());
}

}  // namespace frame
