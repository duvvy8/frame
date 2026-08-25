#include "browser/frame_scheme.h"

#include <windows.h>

#include <string>

#include "include/cef_parser.h"
#include "shared/internal_pages.h"
#include "include/wrapper/cef_stream_resource_handler.h"

namespace frame {
namespace {

const char kSchemeName[] = "frame";

// The directory the build stages internal pages into.
std::wstring ResourceDir() {
  wchar_t path[MAX_PATH] = {};
  ::GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring dir(path);
  const size_t slash = dir.find_last_of(L'\\');
  if (slash != std::wstring::npos) {
    dir = dir.substr(0, slash);
  }
  return dir + L"\\resources\\";
}

// The allowlist itself lives in shared/internal_pages.h so the security
// boundary can be tested without a running browser. Only the URL parsing is
// here, because that part needs CEF.
const internal_pages::Resource* LookupUrl(const CefString& url) {
  CefURLParts parts;
  if (!CefParseURL(url, parts)) {
    return nullptr;
  }
  return internal_pages::Lookup(CefString(&parts.host).ToString(),
                                CefString(&parts.path).ToString());
}

class FrameSchemeFactory : public CefSchemeHandlerFactory {
 public:
  // Declared explicitly: CEF's DISALLOW_COPY_AND_ASSIGN below suppresses the
  // implicit one.
  FrameSchemeFactory() = default;

  CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser,
                                       CefRefPtr<CefFrame> frame,
                                       const CefString& scheme_name,
                                       CefRefPtr<CefRequest> request) override {
    const internal_pages::Resource* resource = LookupUrl(request->GetURL());
    if (!resource) {
      return nullptr;  // Not on the list; CEF reports it as a failed load.
    }

    const std::wstring file =
        ResourceDir() + CefString(resource->name).ToWString();
    CefRefPtr<CefStreamReader> stream =
        CefStreamReader::CreateForFile(CefString(file));
    if (!stream) {
      return nullptr;
    }
    return new CefStreamResourceHandler(resource->mime, stream);
  }

 private:
  IMPLEMENT_REFCOUNTING(FrameSchemeFactory);
  DISALLOW_COPY_AND_ASSIGN(FrameSchemeFactory);
};

}  // namespace

void RegisterFrameScheme(CefRawPtr<CefSchemeRegistrar> registrar) {
  // STANDARD so URLs parse with a host and relative paths resolve against it;
  // SECURE so the pages count as a trusted origin rather than being treated
  // like a local file.
  registrar->AddCustomScheme(kSchemeName,
                             CEF_SCHEME_OPTION_STANDARD |
                                 CEF_SCHEME_OPTION_SECURE |
                                 CEF_SCHEME_OPTION_CORS_ENABLED |
                                 CEF_SCHEME_OPTION_FETCH_ENABLED);
}

void InstallFrameSchemeHandler() {
  // Empty domain: the factory handles every frame:// host.
  CefRegisterSchemeHandlerFactory(kSchemeName, CefString(),
                                  new FrameSchemeFactory());
}

}  // namespace frame
