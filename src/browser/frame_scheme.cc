#include "browser/frame_scheme.h"

#include <windows.h>

#include <string>

#include "include/cef_parser.h"
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

// A FLAT allowlist, deliberately.
//
// Every internal page and asset is named here explicitly. Nothing derives a
// filesystem path from the URL, so no page — however it is navigated to, and
// whatever it embeds — can reach a file that is not on this list. That is the
// whole security property of the scheme: traversal is not filtered, it is
// impossible to express.
struct Resource {
  const char* name;
  const char* mime;
};

const Resource kAllowed[] = {
    {"newtab.html", "text/html"},
    {"shell.css", "text/css"},
    {"shell.js", "text/javascript"},
    {"newtab.css", "text/css"},
    {"logo.svg", "image/svg+xml"},
};

const Resource* Lookup(const std::string& name) {
  for (const Resource& resource : kAllowed) {
    if (name == resource.name) {
      return &resource;
    }
  }
  return nullptr;
}

// frame://newtab      -> newtab.html
// frame://newtab/x.css -> x.css, if x.css is allowlisted
std::string ResolveName(const CefString& url) {
  CefURLParts parts;
  if (!CefParseURL(url, parts)) {
    return std::string();
  }
  const std::string host = CefString(&parts.host).ToString();
  std::string path = CefString(&parts.path).ToString();

  while (!path.empty() && path.front() == '/') {
    path.erase(path.begin());
  }
  if (path.empty()) {
    return host + ".html";
  }
  return path;
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
    const std::string name = ResolveName(request->GetURL());
    const Resource* resource = Lookup(name);
    if (!resource) {
      return nullptr;  // Not on the list; CEF reports it as a failed load.
    }

    const std::wstring file = ResourceDir() + CefString(name).ToWString();
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
