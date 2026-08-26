#ifndef FRAME_BROWSER_WINDOW_LIST_H_
#define FRAME_BROWSER_WINDOW_LIST_H_

#include <cstddef>
#include <functional>

#include "browser/main_window.h"

namespace frame::windows {

// Ownership of every open browser window.
//
// Frame was single-window until Ctrl+N and Ctrl+Shift+N needed a second one,
// and "the app quits when the window closes" was true only because there was
// nothing else to quit for. Both facts move here: the list owns the windows,
// and the message loop ends when the LAST one goes, not the first.

// Creates a window, registers it, and returns it. Null if the native window
// could not be created, in which case nothing is registered.
MainWindow* Open(const MainWindow::Options& options);

// Runs `fn` for every open window.
//
// Settings are process-wide, so a change made in one window has to reach all
// of them. Applying it only where it was clicked leaves a second window
// running on the old value until it is restarted, which is exactly the kind of
// half-applied setting this pass exists to remove.
void ForEach(const std::function<void(MainWindow*)>& fn);

// Called from WM_DESTROY. Deletion is POSTED rather than immediate: the
// window's own window procedure is still on the stack at that point, and
// deleting the object underneath it is a use-after-free that only shows up as
// a crash on close.
void OnWindowDestroyed(MainWindow* window);

std::size_t Count();

}  // namespace frame::windows

#endif  // FRAME_BROWSER_WINDOW_LIST_H_
