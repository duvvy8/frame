#include "browser/chrome_surface.h"

#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <sstream>
#include <string>
#include <vector>

#include "browser/main_window.h"
#include "shared/chrome_layout.h"
#include "shared/shortcuts.h"

namespace frame {
namespace {

// Bridge channel names are constants, never raw literals at call sites, so a
// rename fails to compile instead of silently going unhandled at runtime.
const char kQueryLayout[] = "frame:layout";
const char kQueryShell[] = "frame:shell";
const char kQueryWindowState[] = "frame:window:state";
// A surface asks for the current state once it is ready. Pushes alone are a
// race: everything worth pushing can happen before a surface has finished
// loading, and that surface then never learns anything at all.
const char kQueryBrowserState[] = "browser:state";
const char kCommandMinimize[] = "frame:window:minimize";
const char kCommandMaximize[] = "frame:window:maximize";
const char kCommandClose[] = "frame:window:close";
// Followed by "x,y,w,h;x,y,w,h;..." — a flat list rather than JSON, so the
// browser process needs no parser to read it.
const char kCommandDragRegions[] = "frame:dragregions:";

// Chrome commands. Names are constants for the same reason as above.
const char kCommandSidebarToggle[] = "sidebar:toggle";
const char kCommandTabCreate[] = "tab:create";
const char kCommandTabClose[] = "tab:close:";
const char kCommandTabSelect[] = "tab:select:";
// Followed by "<id>:<index>".
const char kCommandTabReorder[] = "tab:reorder:";
// Followed by "<id>:<x>:<y>", the anchor in SURFACE DIPs.
const char kCommandTabMenu[] = "tab:menu:";
const char kCommandTabSleep[] = "tab:sleep:";
const char kCommandTabWake[] = "tab:wake:";
const char kCommandNavBack[] = "nav:back";
const char kCommandNavForward[] = "nav:forward";
const char kCommandNavReload[] = "nav:reload";
const char kCommandNavStop[] = "nav:stop";
const char kCommandNavGo[] = "nav:go:";
const char kCommandFavoriteAdd[] = "favorite:add:";
const char kCommandFavoriteRemove[] = "favorite:remove:";
const char kCommandFavoriteMove[] = "favorite:move:";

bool StartsWith(const std::string& value, const char* prefix) {
  return value.rfind(prefix, 0) == 0;
}

// Parses "x,y,w,h;x,y,w,h;..." into rectangles, skipping anything malformed
// rather than throwing: a bad region should cost one undraggable strip, not
// the window.
std::vector<layout::IntRect> ParseRegions(const std::string& payload) {
  std::vector<layout::IntRect> regions;
  std::istringstream stream(payload);
  std::string item;
  while (std::getline(stream, item, ';')) {
    if (item.empty()) {
      continue;
    }
    layout::IntRect rect;
    char separator = 0;
    std::istringstream parts(item);
    if ((parts >> rect.x >> separator >> rect.y >> separator >> rect.width >>
         separator >> rect.height)) {
      regions.push_back(rect);
    }
  }
  return regions;
}

// Answers the two things a chrome surface cannot know on its own.
class SurfaceQueryHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  SurfaceQueryHandler(CefRefPtr<WindowRef> window, SurfaceId id)
      : window_ref_(window), id_(id) {}

  bool OnQuery(CefRefPtr<CefBrowser> browser,
               CefRefPtr<CefFrame> frame,
               int64_t query_id,
               const CefString& request,
               bool persistent,
               CefRefPtr<Callback> callback) override {
    const std::string name = request.ToString();

    if (name == kQueryLayout) {
      // Surfaces read geometry from here instead of hardcoding a second copy.
      std::ostringstream json;
      json << "{\"topbarHeight\":" << layout::kTopbarHeight
           << ",\"sidebarWidth\":" << layout::kSidebarWidth
           << ",\"collapsedRailWidth\":" << layout::kCollapsedRailWidth
           << ",\"shellInset\":" << layout::kShellInset
           << ",\"viewportRadius\":" << layout::kViewportRadius
           << ",\"bookmarksHeight\":" << layout::kBookmarksHeight
           << ",\"tabMinWidth\":" << layout::kTabMinWidth
           << ",\"tabFloorWidth\":" << layout::kTabFloorWidth
           << ",\"tabMaxWidth\":" << layout::kTabMaxWidth
           << ",\"tabGap\":" << layout::kTabGap
           << ",\"newTabWidth\":" << layout::kNewTabWidth
           << ",\"sidebarTransitionMs\":" << layout::kSidebarTransitionMs
           << "}";
      callback->Success(json.str());
      return true;
    }

    if (name == kQueryShell) {
      // Where this surface sits inside the window, plus the window size.
      //
      // The ambient shell gradient is anchored to the WINDOW, not to each
      // surface. Without this offset every surface would restart the gradient
      // at its own origin and the glow would visibly break at every seam.
      // Reported in DIPs, because these become CSS pixel values.
      const CefRect bounds =
          window() ? window()->SurfaceBoundsDip(id_) : CefRect(0, 0, 0, 0);
      std::ostringstream json;
      json << "{\"surfaceX\":" << bounds.x << ",\"surfaceY\":" << bounds.y
           << ",\"surfaceWidth\":" << bounds.width
           << ",\"surfaceHeight\":" << bounds.height << ",\"windowWidth\":"
           << (window() ? window()->ClientWidthDip() : 0) << ",\"windowHeight\":"
           << (window() ? window()->ClientHeightDip() : 0) << "}";
      callback->Success(json.str());
      return true;
    }

    // Window commands. These run on the CEF UI thread, which is the same
    // thread the window's message loop runs on, so touching the HWND directly
    // here is safe.
    if (window()) {
      if (name == kQueryWindowState) {
        std::ostringstream json;
        json << "{\"maximized\":"
             << (window()->IsWindowMaximized() ? "true" : "false") << "}";
        callback->Success(json.str());
        return true;
      }
      if (name == kCommandMinimize) {
        window()->Minimize();
        callback->Success("ok");
        return true;
      }
      if (name == kCommandMaximize) {
        window()->ToggleMaximize();
        callback->Success("ok");
        return true;
      }
      if (name == kCommandClose) {
        window()->CloseWindow();
        callback->Success("ok");
        return true;
      }
      if (name == kQueryBrowserState) {
        callback->Success(window()->BrowserStateJson());
        return true;
      }
      if (name == kCommandSidebarToggle) {
        window()->ToggleSidebar();
        callback->Success("ok");
        return true;
      }
      if (name == kCommandTabCreate) {
        window()->CreateTab(std::string(), /*activate=*/true);
        callback->Success("ok");
        return true;
      }
      if (StartsWith(name, kCommandTabClose)) {
        window()->CloseTab(std::atoi(name.c_str() + std::strlen(kCommandTabClose)));
        callback->Success("ok");
        return true;
      }
      if (StartsWith(name, kCommandTabSelect)) {
        window()->SelectTab(
            std::atoi(name.c_str() + std::strlen(kCommandTabSelect)));
        callback->Success("ok");
        return true;
      }
      if (StartsWith(name, kCommandTabMenu)) {
        // "<id>:<x>:<y>". Parsed defensively rather than trusted: this arrives
        // from a renderer, and a malformed payload should cost one menu that
        // did not open, never a crash.
        const std::string payload = name.substr(std::strlen(kCommandTabMenu));
        std::istringstream parts(payload);
        std::string id_text;
        std::string x_text;
        std::string y_text;
        if (std::getline(parts, id_text, ':') &&
            std::getline(parts, x_text, ':') && std::getline(parts, y_text)) {
          window()->ShowTabContextMenu(std::atoi(id_text.c_str()),
                                       std::atoi(x_text.c_str()),
                                       std::atoi(y_text.c_str()));
        }
        callback->Success("ok");
        return true;
      }
      if (StartsWith(name, kCommandTabSleep)) {
        window()->SleepTab(
            std::atoi(name.c_str() + std::strlen(kCommandTabSleep)));
        callback->Success("ok");
        return true;
      }
      if (StartsWith(name, kCommandTabWake)) {
        window()->WakeTab(
            std::atoi(name.c_str() + std::strlen(kCommandTabWake)));
        callback->Success("ok");
        return true;
      }
      if (StartsWith(name, kCommandTabReorder)) {
        const std::string payload =
            name.substr(std::strlen(kCommandTabReorder));
        const size_t split = payload.find(':');
        if (split != std::string::npos) {
          window()->ReorderTab(std::atoi(payload.substr(0, split).c_str()),
                              std::atoi(payload.substr(split + 1).c_str()));
        }
        callback->Success("ok");
        return true;
      }
      if (name == kCommandNavBack) {
        window()->GoBack();
        callback->Success("ok");
        return true;
      }
      if (name == kCommandNavForward) {
        window()->GoForward();
        callback->Success("ok");
        return true;
      }
      if (name == kCommandNavReload) {
        window()->Reload();
        callback->Success("ok");
        return true;
      }
      if (name == kCommandNavStop) {
        window()->StopLoad();
        callback->Success("ok");
        return true;
      }
      if (StartsWith(name, kCommandNavGo)) {
        window()->Navigate(name.substr(std::strlen(kCommandNavGo)));
        callback->Success("ok");
        return true;
      }
      if (StartsWith(name, kCommandFavoriteAdd)) {
        // "favorite:add:<url>\t<title>"
        const std::string payload =
            name.substr(std::strlen(kCommandFavoriteAdd));
        const size_t tab = payload.find('\t');
        if (tab == std::string::npos) {
          window()->AddFavorite(payload, std::string());
        } else {
          window()->AddFavorite(payload.substr(0, tab), payload.substr(tab + 1));
        }
        callback->Success("ok");
        return true;
      }
      if (StartsWith(name, kCommandFavoriteRemove)) {
        window()->RemoveFavorite(name.substr(std::strlen(kCommandFavoriteRemove)));
        callback->Success("ok");
        return true;
      }
      if (StartsWith(name, kCommandFavoriteMove)) {
        // "favorite:move:<from>:<to>"
        const std::string payload =
            name.substr(std::strlen(kCommandFavoriteMove));
        const size_t split = payload.find(':');
        if (split != std::string::npos) {
          window()->MoveFavorite(std::atoi(payload.substr(0, split).c_str()),
                                std::atoi(payload.substr(split + 1).c_str()));
        }
        callback->Success("ok");
        return true;
      }
      if (StartsWith(name, kCommandDragRegions)) {
        // Only the topbar owns the caption strip; a stray report from another
        // surface must not decide where the window can be dragged.
        if (id_ == SurfaceId::kTopbar) {
          window()->SetDragExclusions(
              ParseRegions(name.substr(std::strlen(kCommandDragRegions))));
        }
        callback->Success("ok");
        return true;
      }
    }

    return false;  // Not ours; let another handler try.
  }

 private:
  // Null once the window is gone. A query can be in flight across the
  // window's destruction, so this is checked per call rather than cached.
  MainWindow* window() const { return window_ref_->get(); }

  CefRefPtr<WindowRef> window_ref_;
  const SurfaceId id_;
};

}  // namespace

ChromeSurface::ChromeSurface(CefRefPtr<WindowRef> window, SurfaceId id)
    : window_ref_(window), id_(id) {
  CefMessageRouterConfig config;  // Defaults: window.cefQuery / cefQueryCancel.
  router_ = CefMessageRouterBrowserSide::Create(config);
  query_handler_.reset(new SurfaceQueryHandler(window, id));
  router_->AddHandler(query_handler_.get(), /*first=*/false);
}

bool ChromeSurface::OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                                  const CefKeyEvent& event,
                                  CefEventHandle os_event,
                                  bool* is_keyboard_shortcut) {
  // RAWKEYDOWN only — see the matching comment in PageClient. One press
  // arrives three times.
  if (event.type != KEYEVENT_RAWKEYDOWN) {
    return false;
  }

  shortcuts::Chord chord;
  chord.key = event.windows_key_code;
  chord.ctrl = (event.modifiers & EVENTFLAG_CONTROL_DOWN) != 0;
  chord.shift = (event.modifiers & EVENTFLAG_SHIFT_DOWN) != 0;
  chord.alt = (event.modifiers & EVENTFLAG_ALT_DOWN) != 0;

  const shortcuts::Command command = shortcuts::Match(chord);
  if (command == shortcuts::Command::kNone) {
    return false;
  }

  if (shortcuts::IsEditCommand(command)) {
    CefRefPtr<CefFrame> frame = browser ? browser->GetFocusedFrame() : nullptr;
    if (!frame) {
      return false;
    }
    switch (command) {
      case shortcuts::Command::kCopy:
        frame->Copy();
        return true;
      case shortcuts::Command::kCut:
        frame->Cut();
        return true;
      case shortcuts::Command::kPaste:
        frame->Paste();
        return true;
      case shortcuts::Command::kSelectAll:
        frame->SelectAll();
        return true;
      case shortcuts::Command::kUndo:
        frame->Undo();
        return true;
      case shortcuts::Command::kRedo:
        frame->Redo();
        return true;
      default:
        return false;
    }
  }

  // Escape while typing in the address bar belongs to the surface, which uses
  // it to abandon the edit and restore the real URL. Only the surface knows
  // whether an edit is in progress, so the window never gets a look at it.
  if (command == shortcuts::Command::kStop && id_ == SurfaceId::kSidebar) {
    return false;
  }

  return window() && window()->ExecuteCommand(command);
}

bool ChromeSurface::GetScreenInfo(CefRefPtr<CefBrowser> browser,
                                  CefScreenInfo& screen_info) {
  if (!window()) {
    return false;
  }
  screen_info.device_scale_factor = window()->DeviceScale();
  return true;
}

void ChromeSurface::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) {
  // DIPs, not pixels: CEF multiplies by the device scale factor above to get
  // the bitmap it hands back through OnPaint.
  const CefRect bounds =
      window() ? window()->SurfaceBoundsDip(id_) : CefRect(0, 0, 1, 1);
  // CEF rejects an empty view, so never report zero in either axis.
  rect.x = 0;
  rect.y = 0;
  rect.width = bounds.width > 0 ? bounds.width : 1;
  rect.height = bounds.height > 0 ? bounds.height : 1;
}

void ChromeSurface::OnPaint(CefRefPtr<CefBrowser> browser,
                            PaintElementType type,
                            const RectList& dirty_rects,
                            const void* buffer,
                            int width,
                            int height) {
  // PET_POPUP is for select dropdowns, composited separately. No surface uses
  // one yet.
  if (type != PET_VIEW || !window()) {
    return;
  }
  window()->OnSurfacePaint(id_, buffer, width, height);
}

void ChromeSurface::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  if (window()) {
    window()->SetSurfaceBrowser(id_, browser);
  }
}

void ChromeSurface::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  router_->OnBeforeClose(browser);
  if (window()) {
    // Not SetSurfaceBrowser(nullptr): the window also has to learn that one
    // more of the browsers it is waiting on during a close has finished, and
    // clearing the pointer alone tells it nothing about that.
    window()->OnSurfaceClosed(id_);
  }
}

bool ChromeSurface::OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                                     cef_log_severity_t level,
                                     const CefString& message,
                                     const CefString& source,
                                     int line) {
  // Warnings and errors only. A chrome surface failing silently is the hardest
  // class of bug to see here — a JS exception in an off-screen surface has no
  // visible symptom at all, the controls just stop responding — so the ones
  // that matter are always recorded. Routine console.log output is not, or the
  // file would grow forever for no benefit.
  if (level < LOGSEVERITY_WARNING) {
    return false;
  }

  // Appended next to the executable.
  wchar_t path[MAX_PATH] = {};
  ::GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring dir(path);
  const size_t slash = dir.find_last_of(L'\\');
  if (slash != std::wstring::npos) {
    dir = dir.substr(0, slash);
  }
  const std::wstring log_path = dir + L"\frame-console.log";

  // Bounded rather than append-forever: a surface stuck in an error loop would
  // otherwise write until the disk filled.
  constexpr std::uintmax_t kMaxLogBytes = 256 * 1024;
  std::error_code ec;
  if (std::filesystem::file_size(log_path, ec) > kMaxLogBytes && !ec) {
    std::filesystem::remove(log_path, ec);
  }

  std::ofstream log(log_path, std::ios::app);
  if (log) {
    log << "[" << (id_ == SurfaceId::kTopbar ? "topbar" : "sidebar")
        << "] severity=" << static_cast<int>(level) << " "
        << source.ToString() << ":" << line << " - " << message.ToString()
        << "\n";
  }
  return false;  // Also let CEF handle it normally.
}

bool ChromeSurface::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message) {
  return router_->OnProcessMessageReceived(browser, frame, source_process,
                                           message);
}

}  // namespace frame
