#ifndef FRAME_BROWSER_TOPBAR_CLIENT_H_
#define FRAME_BROWSER_TOPBAR_CLIENT_H_

#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_render_handler.h"
#include "include/wrapper/cef_message_router.h"

namespace frame {

class MainWindow;

// CefClient for the off-screen-rendered topbar surface.
//
// Off-screen rendering is the deliberate choice for chrome surfaces: it keeps
// the chrome as HTML/CSS while the engine underneath is native, and it hands us
// a real bitmap of the surface, which the capture-driven glass effects need.
class TopbarClient : public CefClient,
                     public CefRenderHandler,
                     public CefLifeSpanHandler {
 public:
  explicit TopbarClient(MainWindow* window);

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

  // Detaches from the window during teardown, so a late paint cannot touch a
  // destroyed window.
  void Detach() { window_ = nullptr; }

 private:
  MainWindow* window_;  // Not owned; outlives this client, cleared by Detach().
  CefRefPtr<CefMessageRouterBrowserSide> router_;
  std::unique_ptr<CefMessageRouterBrowserSide::Handler> query_handler_;

  IMPLEMENT_REFCOUNTING(TopbarClient);
  DISALLOW_COPY_AND_ASSIGN(TopbarClient);
};

}  // namespace frame

#endif  // FRAME_BROWSER_TOPBAR_CLIENT_H_
