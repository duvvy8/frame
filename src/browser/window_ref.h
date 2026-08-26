// Frame — a revocable handle to a MainWindow.
//
// Every CEF client Frame creates outlives the window it belongs to. CEF holds
// the last reference to a CefClient and drops it some turns after the browser
// is gone, and a windowed browser's OnBeforeClose can arrive after the native
// window hierarchy has already been torn down. PageClient and ChromeSurface
// therefore cannot hold a MainWindow* and hope.
//
// Both previously declared a Detach() to clear that pointer, and NOTHING EVER
// CALLED EITHER — closing a window left CEF callbacks pointing at freed
// memory. Fixing that by remembering to call Detach() from the right places
// would only work until someone added a third client, so the invariant is
// moved into the type instead: there is exactly one handle per window, the
// window clears it in its own destructor, and a client that asks after that
// gets null rather than a dangling pointer.
//
// Browser-process UI thread only, which is the single thread every window
// procedure and every one of these callbacks runs on. No lock is needed and
// one must not be added on the assumption that ref-counted means shared across
// threads.

#ifndef FRAME_BROWSER_WINDOW_REF_H_
#define FRAME_BROWSER_WINDOW_REF_H_

#include "include/cef_base.h"

namespace frame {

class MainWindow;

class WindowRef : public CefBaseRefCounted {
 public:
  explicit WindowRef(MainWindow* window) : window_(window) {}

  // Null once the window is gone. Callers must check it EVERY time rather than
  // caching the result: a callback that spans a turn of the UI thread can
  // straddle the window's destruction.
  MainWindow* get() const { return window_; }

  void Clear() { window_ = nullptr; }

 private:
  MainWindow* window_;  // Not owned.

  IMPLEMENT_REFCOUNTING(WindowRef);
  DISALLOW_COPY_AND_ASSIGN(WindowRef);
};

}  // namespace frame

#endif  // FRAME_BROWSER_WINDOW_REF_H_
