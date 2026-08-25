#include "browser/frame_app.h"

#include <windows.h>

#include <cstdlib>
#include <string>

#include "browser/main_window.h"
#include "browser/topbar_client.h"
#include "include/cef_browser.h"
#include "include/cef_command_line.h"
#include "shared/chrome_layout.h"

namespace frame {
namespace {

// Directory containing the executable, which is where the build stages the
// chrome surface HTML/CSS.
std::wstring ExecutableDir() {
  wchar_t path[MAX_PATH] = {};
  ::GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring result(path);
  const size_t slash = result.find_last_of(L'\\');
  return slash == std::wstring::npos ? result : result.substr(0, slash);
}

// file:// for now. The frame:// scheme handler, with its flat allowlist, lands
// in a later migration step and replaces this.
std::string TopbarUrl() {
  std::wstring dir = ExecutableDir();
  for (auto& ch : dir) {
    if (ch == L'\\') {
      ch = L'/';
    }
  }
  const std::wstring url = L"file:///" + dir + L"/resources/topbar.html";
  return CefString(url).ToString();
}

int SwitchAsInt(CefRefPtr<CefCommandLine> cmd,
                const char* name,
                int fallback) {
  if (!cmd->HasSwitch(name)) {
    return fallback;
  }
  const std::string value = cmd->GetSwitchValue(name).ToString();
  return value.empty() ? fallback : std::atoi(value.c_str());
}

}  // namespace

FrameApp::FrameApp() = default;
FrameApp::~FrameApp() = default;

void FrameApp::OnContextInitialized() {
  CefRefPtr<CefCommandLine> cmd = CefCommandLine::GetGlobalCommandLine();

  MainWindow::Options options;
  options.width = SwitchAsInt(cmd, "width", options.width);
  options.height = SwitchAsInt(cmd, "height", options.height);
  options.x = SwitchAsInt(cmd, "x", options.x);
  options.y = SwitchAsInt(cmd, "y", options.y);
  options.no_activate = cmd->HasSwitch("no-activate");

  main_window_.reset(new MainWindow(options));
  if (!main_window_->Create(::GetModuleHandleW(nullptr))) {
    return;
  }

  CefRefPtr<TopbarClient> client(new TopbarClient(main_window_.get()));

  CefWindowInfo window_info;
  // Off-screen rendering: CEF hands us pixels instead of owning a child HWND,
  // which is what lets the native window composite chrome surfaces itself.
  window_info.SetAsWindowless(main_window_->hwnd());

  CefBrowserSettings browser_settings;
  browser_settings.windowless_frame_rate = 60;
  browser_settings.background_color = CefColorSetARGB(255, 20, 20, 22);

  CefBrowserHost::CreateBrowser(window_info, client, TopbarUrl(),
                                browser_settings, nullptr, nullptr);
}

void FrameApp::OnWebKitInitialized() {
  // Renderer-process half of the bridge. Config must match the browser side.
  CefMessageRouterConfig config;
  renderer_router_ = CefMessageRouterRendererSide::Create(config);
}

void FrameApp::OnContextCreated(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefRefPtr<CefV8Context> context) {
  if (renderer_router_) {
    renderer_router_->OnContextCreated(browser, frame, context);
  }
}

void FrameApp::OnContextReleased(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 CefRefPtr<CefV8Context> context) {
  if (renderer_router_) {
    renderer_router_->OnContextReleased(browser, frame, context);
  }
}

bool FrameApp::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                        CefRefPtr<CefFrame> frame,
                                        CefProcessId source_process,
                                        CefRefPtr<CefProcessMessage> message) {
  if (renderer_router_) {
    return renderer_router_->OnProcessMessageReceived(browser, frame,
                                                      source_process, message);
  }
  return false;
}

}  // namespace frame
