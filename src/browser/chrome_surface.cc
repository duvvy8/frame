#include "browser/chrome_surface.h"

#include <sstream>

#include "browser/main_window.h"
#include "shared/chrome_layout.h"

namespace frame {
namespace {

// Bridge channel names are constants, never raw literals at call sites, so a
// rename fails to compile instead of silently going unhandled at runtime.
const char kQueryLayout[] = "frame:layout";
const char kQueryShell[] = "frame:shell";

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
