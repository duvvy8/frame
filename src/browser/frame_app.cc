#include "browser/frame_app.h"

#include <windows.h>

#include <cstdlib>
#include <string>

#include "browser/chrome_surface.h"
#include "browser/content_filter.h"
#include "browser/favorites.h"
#include "browser/frame_scheme.h"
#include "browser/main_window.h"
#include "fingerprint_guard_script.h"
#include "browser/window_list.h"
#include "include/cef_browser.h"
#include "include/cef_command_line.h"
#include "shared/chrome_layout.h"

namespace frame {
namespace {

int SwitchAsInt(CefRefPtr<CefCommandLine> cmd, const char* name, int fallback) {
  if (!cmd->HasSwitch(name)) {
    return fallback;
  }
  const std::string value = cmd->GetSwitchValue(name).ToString();
  return value.empty() ? fallback : std::atoi(value.c_str());
}

}  // namespace

FrameApp::FrameApp() = default;
FrameApp::~FrameApp() = default;

void FrameApp::OnBeforeCommandLineProcessing(
    const CefString& process_type,
    CefRefPtr<CefCommandLine> command_line) {
  // Telemetry switches, applied to every process.
  //
  // These are the reason the rewrite exists: Chromium's defaults phone home
  // on a timer, and a browser built for privacy should not inherit that just
  // because it was the default. Each one is a documented Chromium switch, not
  // a guess.
  //
  // This is the minimum, not the whole privacy layer — request-level blocking
  // and per-tab contexts are separate work. But it stops the browser talking
  // to anyone on its own initiative.
  static const char* kSwitches[] = {
      // The component updater's background check-ins, variations pings, and
      // most of the "phone home on a timer" behaviour.
      "disable-background-networking",
      "disable-client-side-phishing-detection",
      "disable-sync",
      "disable-default-apps",
      // Hyperlink auditing pings.
      "no-pings",
      // Chromium's own network-health beacon, on by default upstream.
      "disable-domain-reliability",
      "disable-breakpad",
      // Safe Browsing is a genuine tradeoff rather than a free win: its
      // real-time mode sends visited URLs to Google. Off by default, to be
      // exposed as an explicit opt-in with the tradeoff spelled out.
      "safebrowsing-disable-auto-update",
  };

  for (const char* item : kSwitches) {
    const std::string entry(item);
    const size_t equals = entry.find('=');
    if (equals == std::string::npos) {
      command_line->AppendSwitch(entry);
    } else {
      command_line->AppendSwitchWithValue(entry.substr(0, equals),
                                          entry.substr(equals + 1));
    }
  }

  // Metrics are never initialised rather than being switched off after the
  // fact: starting from a build that was never wired up is a stronger
  // position than disabling something already running.
  command_line->AppendSwitch("disable-metrics");
  command_line->AppendSwitch("disable-metrics-reporting");

  // --disable-features is MERGED, never assigned.
  //
  // CEF has already put its own required entries in this switch by the time we
  // are called, and AppendSwitchWithValue replaces the whole value rather than
  // adding to it. Overwriting it takes out CEF's own configuration and the
  // browser process dies with an access violation before a window ever
  // appears — which is exactly what happened when this was written the
  // obvious way.
  static const char* kDisableFeatures[] = {
      "Translate",
      "OptimizationHints",
      "MediaRouter",
      "OptimizationGuideModelDownloading",
  };

  std::string features = command_line->GetSwitchValue("disable-features");
  for (const char* feature : kDisableFeatures) {
    if (features.find(feature) != std::string::npos) {
      continue;
    }
    if (!features.empty()) {
      features += ",";
    }
    features += feature;
  }
  command_line->AppendSwitchWithValue("disable-features", features);
}

void FrameApp::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
  // Runs in EVERY process, before CEF starts. A renderer that has not been
  // told about the scheme will not treat frame:// pages as a real origin.
  RegisterFrameScheme(registrar);
}

void FrameApp::OnContextInitialized() {
  // The factory can only be installed once CEF is up.
  InstallFrameSchemeHandler();

  // Loaded BEFORE any browser exists, and never touched again. CEF consults
  // the filter on the IO thread for every subresource, so the one moment it is
  // safe to write to is right here, while nothing can be asking.
  content_filter::Load(ProfileDir());

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

  options.incognito = cmd->HasSwitch("incognito");

  // The window list owns every window from here on, including the first: making
  // the original a special case owned by the application is what would leave
  // Ctrl+N with nowhere to put a second one.
  MainWindow* window = windows::Open(options);
  if (!window) {
    return;
  }

  // Frame's own start page, served over frame:// from the flat allowlist.
  const std::string start_url = cmd->HasSwitch("url")
                                    ? cmd->GetSwitchValue("url").ToString()
                                    : "frame://newtab";
  window->CreateTab(start_url, /*activate=*/true);
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

  // Fingerprint minimisation, in the RENDERER, at context creation.
  //
  // This is the earliest point at which JavaScript can run in a document, so
  // the values are already corrected before any page script observes them —
  // there is no window in which the real ones are visible, and no observer or
  // timer left running afterwards. One evaluation per context; nothing per
  // request, and nothing added to the page's own DOM for it to notice.
  //
  // Frame's own pages are skipped. They have no fingerprinting to defend
  // against and the guard would only be one more thing loaded on a new tab.
  if (!frame) {
    return;
  }
  const std::string url = frame->GetURL().ToString();
  if (url.rfind("frame://", 0) == 0 || url.rfind("devtools://", 0) == 0) {
    return;
  }

  CefRefPtr<CefV8Value> ignored;
  CefRefPtr<CefV8Exception> exception;
  // Failure is survivable and must never take the page with it: if the guard
  // throws, the page keeps its real values rather than getting a broken
  // navigator.
  context->Eval(kFingerprintGuardScript, "frame://fingerprint-guard", 0,
                ignored, exception);
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
