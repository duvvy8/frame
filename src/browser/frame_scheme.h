#ifndef FRAME_BROWSER_FRAME_SCHEME_H_
#define FRAME_BROWSER_FRAME_SCHEME_H_

#include "include/cef_request_context_handler.h"
#include "include/cef_scheme.h"

namespace frame {

// The frame:// scheme, which serves Frame's own internal pages.
//
// Registration happens in two places, and both are required: the scheme has to
// be declared in every process before CEF starts, and the factory that
// actually serves it is installed in the browser process afterwards.
void RegisterFrameScheme(CefRawPtr<CefSchemeRegistrar> registrar);
void InstallFrameSchemeHandler();

// Handler that installs the same factory on a NON-GLOBAL request context.
//
// Scheme handler factories are registered per request context, and
// InstallFrameSchemeHandler() only ever reaches the global one. An incognito
// window creates its own context, which therefore has never heard of frame://
// — so its first tab failed with ERR_UNKNOWN_URL_SCHEME and landed on the
// unreachable page, which is itself a frame:// URL it also could not load.
//
// Registration is deferred to OnRequestContextInitialized rather than done
// straight after CreateContext, because the context is not ready to accept a
// factory until then.
CefRefPtr<CefRequestContextHandler> CreateSchemeContextHandler();

}  // namespace frame

#endif  // FRAME_BROWSER_FRAME_SCHEME_H_
