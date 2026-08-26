// Frame — Windows entry point.
//
// Mirrors the process model CEF 151 actually uses. Note the two entry points
// below: with the sandbox enabled (the default, and what we want) the app is
// built as a DLL and launched by CEF's bootstrap.exe, which calls RunWinMain.
// The wWinMain path is only used in unsandboxed builds.

#include <windows.h>

#include "browser/favorites.h"
#include "browser/frame_app.h"
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/cef_sandbox_win.h"
#include "include/cef_version_info.h"

namespace {

int RunMain(HINSTANCE instance, void* sandbox_info) {
  CefMainArgs main_args(instance);

  // Constructed BEFORE CefExecuteProcess, and passed to it, because this same
  // object serves the renderer process too. Passing nullptr here (as the
  // cefsimple sample does, since its app has no renderer-side handler) leaves
  // the renderer with no CefRenderProcessHandler, so OnWebKitInitialized never
  // runs and the JS side of the bridge is never registered.
  CefRefPtr<frame::FrameApp> app(new frame::FrameApp);

  // Every CEF process is this same binary re-executed with a different --type.
  // For a sub-process this runs its logic and returns; only the browser process
  // continues past here.
  const int exit_code = CefExecuteProcess(main_args, app.get(), sandbox_info);
  if (exit_code >= 0) {
    return exit_code;
  }

  CefSettings settings;
  if (!sandbox_info) {
    settings.no_sandbox = true;
  }

  // Required for the OSR chrome surfaces. Without it CreateBrowser rejects a
  // windowless CefWindowInfo.
  settings.windowless_rendering_enabled = true;

  // Where the profile lives.
  //
  // Left unset, CEF picks a default and warns at every startup that this "may
  // lead to unintended process singleton behavior" — two copies of Frame would
  // be fighting over the same implicit directory. It also meant no persistent
  // HTTP cache: every run re-fetched everything it had already seen.
  //
  // Pointed at the same directory the favourites and favicons already use, so
  // Frame has ONE profile location rather than one it chose and one CEF
  // guessed. Incognito windows are unaffected — they get their own in-memory
  // request context and never touch this.
  CefString(&settings.root_cache_path) = frame::ProfileDir();

  if (!CefInitialize(main_args, settings, app.get(), sandbox_info)) {
    return CefGetExitCode();
  }

  CefRunMessageLoop();
  CefShutdown();
  return 0;
}

}  // namespace

#if defined(CEF_USE_BOOTSTRAP)

// Called by bootstrap.exe when built as a DLL (the sandboxed configuration).
CEF_BOOTSTRAP_EXPORT int RunWinMain(HINSTANCE hInstance,
                                    LPTSTR lpCmdLine,
                                    int nCmdShow,
                                    void* sandbox_info,
                                    cef_version_info_t* /*version_info*/) {
  return RunMain(hInstance, sandbox_info);
}

#else  // !defined(CEF_USE_BOOTSTRAP)

int APIENTRY wWinMain(HINSTANCE hInstance,
                      HINSTANCE hPrevInstance,
                      LPTSTR lpCmdLine,
                      int nCmdShow) {
  UNREFERENCED_PARAMETER(hPrevInstance);
  UNREFERENCED_PARAMETER(lpCmdLine);
  UNREFERENCED_PARAMETER(nCmdShow);

  void* sandbox_info = nullptr;
#if defined(CEF_USE_SANDBOX)
  CefScopedSandboxInfo scoped_sandbox;
  sandbox_info = scoped_sandbox.sandbox_info();
#endif

  return RunMain(hInstance, sandbox_info);
}

#endif  // !defined(CEF_USE_BOOTSTRAP)
