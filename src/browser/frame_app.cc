#include "browser/frame_app.h"

#include <windows.h>

#include <cstdlib>
#include <string>

#include "browser/chrome_surface.h"
#include "browser/main_window.h"
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
std::string SurfaceUrl(const wchar_t* file_name) {
  std::wstring dir = ExecutableDir();
  for (auto& ch : dir) {
    if (ch == L'\\') {
      ch = L'/';
    }
  }
  const std::wstring url =
      L"file:///" + dir + L"/resources/" + file_name;
  return CefString(url).ToString();
}

int SwitchAsInt(CefRefPtr<CefCommandLine> cmd, const char* name, int fallback) {
  if (!cmd->HasSwitch(name)) {
    return fallback;
  }
  const std::string value = cmd->GetSwitchValue(name).ToString();
  return value.empty() ? fallback : std::atoi(value.c_str());
}

void CreateSurface(MainWindow* window, SurfaceId id, const wchar_t* file_name) {
  CefRefPtr<ChromeSurface> surface(new ChromeSurface(window, id));

  CefWindowInfo window_info;
  // Off-screen rendering: CEF hands us pixels instead of owning a child HWND,
  // which is what lets the native window composite the surfaces itself.
  window_info.SetAsWindowless(window->hwnd());

  CefBrowserSettings settings;
  settings.windowless_frame_rate = 60;
  settings.background_color = CefColorSetARGB(255, 11, 11, 13);

  CefBrowserHost::CreateBrowser(window_info, surface, SurfaceUrl(file_name),
                                settings, nullptr, nullptr);
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
  // Escape hatch if custom hit-testing ever misbehaves: --system-titlebar
  // gives back a window that can always be moved, resized and closed.
  options.system_titlebar = cmd->HasSwitch("system-titlebar");

  main_window_.reset(new MainWindow(options));
  if (!main_window_->Create(::GetModuleHandleW(nullptr))) {
    return;
  }

  // Each chrome surface is its own off-screen browser, composited by the
  // window. Independent surfaces mean one can repaint without touching the
  // others.
  CreateSurface(main_window_.get(), SurfaceId::kTopbar, L"topbar.html");
  CreateSurface(main_window_.get(), SurfaceId::kSidebar, L"sidebar.html");

  // Open on a real page. frame://newtab replaces this default once the scheme
  // handler and internal pages land.
  const std::string start_url = cmd->HasSwitch("url")
                                    ? cmd->GetSwitchValue("url").ToString()
                                    : "https://www.google.com";
  main_window_->CreateTab(start_url, /*activate=*/true);
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
