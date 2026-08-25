#include "browser/window_list.h"

#include <windows.h>

#include <algorithm>
#include <memory>
#include <vector>

#include <string>

#include "browser/chrome_surface.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_task.h"

namespace frame::windows {
namespace {

// Chrome surfaces are served over frame:// like every other internal page, so
// all of Frame's own UI shares one trusted origin. Over file:// they were an
// opaque origin, which among other things left the clipboard API unavailable.
std::string SurfaceUrl(const char* host) {
  return std::string("frame://") + host;
}

// One off-screen browser per chrome surface, composited by the window.
// Independent surfaces mean one can repaint without touching the others.
void CreateSurface(MainWindow* window, SurfaceId id, const char* host) {
  CefRefPtr<ChromeSurface> surface(new ChromeSurface(window, id));

  CefWindowInfo window_info;
  // Off-screen rendering: CEF hands us pixels instead of owning a child HWND,
  // which is what lets the native window composite the surfaces itself.
  window_info.SetAsWindowless(window->hwnd());

  CefBrowserSettings settings;
  settings.windowless_frame_rate = 60;
  settings.background_color = CefColorSetARGB(255, 11, 11, 13);

  CefBrowserHost::CreateBrowser(window_info, surface, SurfaceUrl(host),
                                settings, nullptr, nullptr);
}

// Browser-process, CEF UI thread only — which is the same thread every window
// procedure runs on, so no lock is needed and one must not be added on the
// assumption that "static means shared".
std::vector<std::unique_ptr<MainWindow>>& All() {
  static std::vector<std::unique_ptr<MainWindow>> list;
  return list;
}

// Carries the doomed window to the next turn of the UI thread. See the comment
// on OnWindowDestroyed().
class DeleteWindowTask : public CefTask {
 public:
  explicit DeleteWindowTask(std::unique_ptr<MainWindow> window)
      : window_(std::move(window)) {}

  void Execute() override { window_.reset(); }

 private:
  std::unique_ptr<MainWindow> window_;

  IMPLEMENT_REFCOUNTING(DeleteWindowTask);
  DISALLOW_COPY_AND_ASSIGN(DeleteWindowTask);
};

}  // namespace

MainWindow* Open(const MainWindow::Options& options) {
  auto window = std::make_unique<MainWindow>(options);
  if (!window->Create(::GetModuleHandleW(nullptr))) {
    return nullptr;
  }
  MainWindow* raw = window.get();
  All().push_back(std::move(window));

  // A window without its chrome is not a usable window, so the surfaces are
  // part of opening one rather than something the caller has to remember.
  CreateSurface(raw, SurfaceId::kTopbar, "topbar");
  CreateSurface(raw, SurfaceId::kSidebar, "sidebar");
  return raw;
}

void OnWindowDestroyed(MainWindow* window) {
  auto& list = All();
  const auto it = std::find_if(
      list.begin(), list.end(),
      [window](const std::unique_ptr<MainWindow>& item) {
        return item.get() == window;
      });

  if (it != list.end()) {
    std::unique_ptr<MainWindow> doomed = std::move(*it);
    list.erase(it);
    CefPostTask(TID_UI, new DeleteWindowTask(std::move(doomed)));
  }

  // The last window closing is what ends the application. Quitting on the
  // first one would take every other window down with it.
  if (list.empty()) {
    CefQuitMessageLoop();
  }
}

std::size_t Count() {
  return All().size();
}

}  // namespace frame::windows
