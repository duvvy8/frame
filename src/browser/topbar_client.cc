#include "browser/topbar_client.h"

#include <sstream>

#include "browser/main_window.h"
#include "shared/chrome_layout.h"

namespace frame {
namespace {

// Browser-process end of the JS <-> C++ bridge.
//
// Channel names are constants, never raw string literals at call sites — the
// same rule the previous implementation enforced for its IPC channel list, and
// the reason a renamed channel fails to compile instead of silently going
// unhandled at runtime.
const char kQueryLayout[] = "frame:layout";

class TopbarQueryHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  bool OnQuery(CefRefPtr<CefBrowser> browser,
               CefRefPtr<CefFrame> frame,
               int64_t query_id,
               const CefString& request,
               bool persistent,
               CefRefPtr<Callback> callback) override {
    if (request != kQueryLayout) {
      return false;  // Not ours; let another handler try.
    }

    // Answers with the shared layout constants so the chrome surface never
    // hardcodes its own copy. One source of truth, asked for at runtime.
    std::ostringstream json;
    json << "{\"topbarHeight\":" << layout::kTopbarHeight
         << ",\"sidebarWidth\":" << layout::kSidebarWidth
         << ",\"collapsedRailWidth\":" << layout::kCollapsedRailWidth
         << ",\"viewportRadius\":" << layout::kViewportRadius
         << ",\"tabMinWidth\":" << layout::kTabMinWidth
         << ",\"tabMaxWidth\":" << layout::kTabMaxWidth
         << ",\"tabGap\":" << layout::kTabGap
         << ",\"sidebarTransitionMs\":" << layout::kSidebarTransitionMs << "}";

    callback->Success(json.str());
    return true;
  }
};

}  // namespace

TopbarClient::TopbarClient(MainWindow* window) : window_(window) {
  CefMessageRouterConfig config;  // Defaults: window.cefQuery / cefQueryCancel.
  router_ = CefMessageRouterBrowserSide::Create(config);
  query_handler_.reset(new TopbarQueryHandler());
  router_->AddHandler(query_handler_.get(), /*first=*/false);
}

void TopbarClient::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) {
  // The topbar spans the full window width at the shared constant height.
  // CEF rejects an empty view, so clamp to at least one pixel.
  const int width = window_ ? window_->client_width() : 0;
  rect.x = 0;
  rect.y = 0;
  rect.width = width > 0 ? width : 1;
  rect.height = layout::kTopbarHeight;
}

void TopbarClient::OnPaint(CefRefPtr<CefBrowser> browser,
                           PaintElementType type,
                           const RectList& dirty_rects,
                           const void* buffer,
                           int width,
                           int height) {
  // PET_POPUP is for select dropdowns and the like, composited separately.
  // The topbar has none yet, so only the view is forwarded.
  if (type != PET_VIEW || !window_) {
    return;
  }
  window_->OnTopbarPaint(buffer, width, height);
}

void TopbarClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  if (window_) {
    window_->SetTopbarBrowser(browser);
  }
}

void TopbarClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  router_->OnBeforeClose(browser);
  if (window_) {
    window_->SetTopbarBrowser(nullptr);
  }
}

bool TopbarClient::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message) {
  return router_->OnProcessMessageReceived(browser, frame, source_process,
                                           message);
}

}  // namespace frame
