#ifndef FRAME_BROWSER_FRAME_APP_H_
#define FRAME_BROWSER_FRAME_APP_H_

#include <memory>

#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/cef_scheme.h"
#include "include/wrapper/cef_message_router.h"

namespace frame {

class MainWindow;

// Application-level callbacks for BOTH the browser and the renderer process.
// One class serves both roles because every CEF process is the same binary
// re-executed with a different --type; the process type decides which handler
// CEF asks for.
class FrameApp : public CefApp,
                 public CefBrowserProcessHandler,
                 public CefRenderProcessHandler {
 public:
  FrameApp();
  ~FrameApp() override;

  // CefApp
  void OnBeforeCommandLineProcessing(
      const CefString& process_type,
      CefRefPtr<CefCommandLine> command_line) override;
  void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override;
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
    return this;
  }

  // CefBrowserProcessHandler
  void OnContextInitialized() override;

  // CefRenderProcessHandler
  void OnWebKitInitialized() override;
  void OnContextCreated(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefV8Context> context) override;
  void OnContextReleased(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         CefRefPtr<CefV8Context> context) override;
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;

 private:
  // Renderer-process half of the JS <-> C++ bridge. Only ever non-null in a
  // renderer process.
  CefRefPtr<CefMessageRouterRendererSide> renderer_router_;

  // Windows are owned by browser/window_list.h, not by the application: they
  // outlive nothing here, and there can be more than one of them.

  IMPLEMENT_REFCOUNTING(FrameApp);
  DISALLOW_COPY_AND_ASSIGN(FrameApp);
};

}  // namespace frame

#endif  // FRAME_BROWSER_FRAME_APP_H_
