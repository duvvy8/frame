#ifndef FRAME_BROWSER_CHROME_SURFACE_H_
#define FRAME_BROWSER_CHROME_SURFACE_H_

#include <memory>

#include "include/cef_client.h"
#include "include/cef_display_handler.h"
#include "include/cef_keyboard_handler.h"
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
                      public CefLifeSpanHandler,
                      public CefDisplayHandler,
                      public CefKeyboardHandler {
 public:
  ChromeSurface(MainWindow* window, SurfaceId id);

  // CefClient
  CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }

  // CefKeyboardHandler
  //
  // Shortcuts pressed while the address field has the caret, plus the clipboard
  // and undo group — which this surface DOES handle, unlike a page. An
  // off-screen browser receives its keys through SendKeyEvent rather than from
  // a focused native window, and Blink's editing shortcuts do not fire reliably
  // for synthesised events, so Ctrl+C in the address bar has to be routed to
  // CefFrame explicitly or it silently does nothing.
  bool OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                     const CefKeyEvent& event,
                     CefEventHandle os_event,
                     bool* is_keyboard_shortcut) override;
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;

  // CefRenderHandler
  void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
  // Without this an off-screen surface always rasterises at 1x and looks soft
  // on a scaled display, however correct its layout is.
  bool GetScreenInfo(CefRefPtr<CefBrowser> browser,
                     CefScreenInfo& screen_info) override;
  void OnPaint(CefRefPtr<CefBrowser> browser,
               PaintElementType type,
               const RectList& dirty_rects,
               const void* buffer,
               int width,
               int height) override;

  // CefLifeSpanHandler
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

  // CefDisplayHandler. A chrome surface is our own code, so anything it logs
  // is a bug worth seeing rather than page noise to ignore.
  bool OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                        cef_log_severity_t level,
                        const CefString& message,
                        const CefString& source,
                        int line) override;

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
