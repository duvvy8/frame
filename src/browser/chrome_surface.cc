#include "browser/chrome_surface.h"

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "browser/main_window.h"
#include "shared/chrome_layout.h"

namespace frame {
namespace {

// Bridge channel names are constants, never raw literals at call sites, so a
// rename fails to compile instead of silently going unhandled at runtime.
const char kQueryLayout[] = "frame:layout";
const char kQueryShell[] = "frame:shell";
const char kQueryWindowState[] = "frame:window:state";
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
const char kCommandNavBack[] = "nav:back";
const char kCommandNavForward[] = "nav:forward";
const char kCommandNavReload[] = "nav:reload";
const char kCommandNavStop[] = "nav:stop";
const char kCommandNavGo[] = "nav:go:";

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
  SurfaceQueryHandler(MainWindow* window, SurfaceId id)
      : window_(window), id_(id) {}

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
           << ",\"viewportRadius\":" << layout::kViewportRadius
           << ",\"bookmarksHeight\":" << layout::kBookmarksHeight
           << ",\"tabMinWidth\":" << layout::kTabMinWidth
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
      const CefRect bounds =
          window_ ? window_->SurfaceBounds(id_) : CefRect(0, 0, 0, 0);
      std::ostringstream json;
      json << "{\"surfaceX\":" << bounds.x << ",\"surfaceY\":" << bounds.y
           << ",\"surfaceWidth\":" << bounds.width
           << ",\"surfaceHeight\":" << bounds.height << ",\"windowWidth\":"
           << (window_ ? window_->client_width() : 0) << ",\"windowHeight\":"
           << (window_ ? window_->client_height() : 0) << "}";
      callback->Success(json.str());
      return true;
    }

    // Window commands. These run on the CEF UI thread, which is the same
    // thread the window's message loop runs on, so touching the HWND directly
    // here is safe.
    if (window_) {
      if (name == kQueryWindowState) {
        std::ostringstream json;
        json << "{\"maximized\":"
             << (window_->IsWindowMaximized() ? "true" : "false") << "}";
        callback->Success(json.str());
        return true;
      }
      if (name == kCommandMinimize) {
        window_->Minimize();
        callback->Success("ok");
        return true;
      }
      if (name == kCommandMaximize) {
        window_->ToggleMaximize();
        callback->Success("ok");
        return true;
      }
      if (name == kCommandClose) {
        window_->CloseWindow();
        callback->Success("ok");
        return true;
      }
      if (name == kCommandSidebarToggle) {
        window_->ToggleSidebar();
        callback->Success("ok");
        return true;
      }
      if (name == kCommandTabCreate) {
        window_->CreateTab(std::string(), /*activate=*/true);
        callback->Success("ok");
        return true;
      }
      if (StartsWith(name, kCommandTabClose)) {
        window_->CloseTab(std::atoi(name.c_str() + std::strlen(kCommandTabClose)));
        callback->Success("ok");
        return true;
      }
      if (StartsWith(name, kCommandTabSelect)) {
        window_->SelectTab(
            std::atoi(name.c_str() + std::strlen(kCommandTabSelect)));
        callback->Success("ok");
        return true;
      }
      if (name == kCommandNavBack) {
        window_->GoBack();
        callback->Success("ok");
        return true;
      }
      if (name == kCommandNavForward) {
        window_->GoForward();
        callback->Success("ok");
        return true;
      }
      if (name == kCommandNavReload) {
        window_->Reload();
        callback->Success("ok");
        return true;
      }
      if (name == kCommandNavStop) {
        window_->StopLoad();
        callback->Success("ok");
        return true;
      }
      if (StartsWith(name, kCommandNavGo)) {
        window_->Navigate(name.substr(std::strlen(kCommandNavGo)));
        callback->Success("ok");
        return true;
      }
      if (StartsWith(name, kCommandDragRegions)) {
        // Only the topbar owns the caption strip; a stray report from another
        // surface must not decide where the window can be dragged.
        if (id_ == SurfaceId::kTopbar) {
          window_->SetDragExclusions(
              ParseRegions(name.substr(std::strlen(kCommandDragRegions))));
        }
        callback->Success("ok");
        return true;
      }
    }

    return false;  // Not ours; let another handler try.
  }

 private:
  MainWindow* window_;  // Not owned; outlives the surface.
  const SurfaceId id_;
};

}  // namespace

ChromeSurface::ChromeSurface(MainWindow* window, SurfaceId id)
    : window_(window), id_(id) {
  CefMessageRouterConfig config;  // Defaults: window.cefQuery / cefQueryCancel.
  router_ = CefMessageRouterBrowserSide::Create(config);
  query_handler_.reset(new SurfaceQueryHandler(window, id));
  router_->AddHandler(query_handler_.get(), /*first=*/false);
}

void ChromeSurface::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) {
  const CefRect bounds =
      window_ ? window_->SurfaceBounds(id_) : CefRect(0, 0, 1, 1);
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
  if (type != PET_VIEW || !window_) {
    return;
  }
  window_->OnSurfacePaint(id_, buffer, width, height);
}

void ChromeSurface::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  if (window_) {
    window_->SetSurfaceBrowser(id_, browser);
  }
}

void ChromeSurface::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  router_->OnBeforeClose(browser);
  if (window_) {
    window_->SetSurfaceBrowser(id_, nullptr);
  }
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
