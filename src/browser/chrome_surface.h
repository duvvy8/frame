#ifndef FRAME_BROWSER_CHROME_SURFACE_H_
#define FRAME_BROWSER_CHROME_SURFACE_H_

#include <memory>

#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_render_handler.h"
#include "include/wrapper/cef_message_router.h"

namespace frame {

class MainWindow;

// Identifies one chrome surface. Each is an independent off-screen browser
// composited by MainWindow, mirroring the design where the window is assembled
// from several separately rendered pieces rather than one page.
enum class SurfaceId {
  kTopbar = 0,
  kSidebar = 1,
  kCount = 2,
};

// CefClient for a single off-screen chrome surface.
//
// The surface does not decide where it lives: it asks MainWindow for its bounds
// so that all geometry stays derived from the shared layout constants, with no
// second copy anywhere.
class ChromeSurface : public CefClient,
                      public CefRenderHandler,
                      public CefLifeSpanHandler {
 public:
  ChromeSurface(MainWindow* window, SurfaceId id);

  // CefClient
  CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;

  // CefRenderHandler
  void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
  void OnPaint(CefRefPtr<CefBrowser> browser,
               PaintElementType type,
               const RectList& dirty_rects,
               const void* buffer,
               int width,
               int height) override;

  // CefLifeSpanHandler
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

  void Detach() { window_ = nullptr; }

 private:
  MainWindow* window_;  // Not owned; cleared by Detach() during teardown.
  const SurfaceId id_;
  CefRefPtr<CefMessageRouterBrowserSide> router_;
  std::unique_ptr<CefMessageRouterBrowserSide::Handler> query_handler_;

  IMPLEMENT_REFCOUNTING(ChromeSurface);
  DISALLOW_COPY_AND_ASSIGN(ChromeSurface);
};

}  // namespace frame

#endif  // FRAME_BROWSER_CHROME_SURFACE_H_
