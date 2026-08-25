#ifndef FRAME_BROWSER_FRAME_SCHEME_H_
#define FRAME_BROWSER_FRAME_SCHEME_H_

#include "include/cef_scheme.h"

namespace frame {

// The frame:// scheme, which serves Frame's own internal pages.
//
// Registration happens in two places, and both are required: the scheme has to
// be declared in every process before CEF starts, and the factory that
// actually serves it is installed in the browser process afterwards.
void RegisterFrameScheme(CefRawPtr<CefSchemeRegistrar> registrar);
void InstallFrameSchemeHandler();

}  // namespace frame

#endif  // FRAME_BROWSER_FRAME_SCHEME_H_
