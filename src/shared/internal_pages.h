// Frame — the frame:// allowlist.
//
// Framework-free so the security boundary can be tested directly instead of
// only through a running browser.

#ifndef FRAME_SHARED_INTERNAL_PAGES_H_
#define FRAME_SHARED_INTERNAL_PAGES_H_

#include <string>

namespace frame::internal_pages {

struct Resource {
  const char* name;
  const char* mime;
};

// A FLAT allowlist, deliberately.
//
// Every internal page and asset is named here explicitly, and nothing derives
// a filesystem path from the URL. No page — however it is reached, and whatever
// it embeds — can load a file that is not on this list. Traversal is not
// filtered out, it is impossible to express: a name containing a separator
// simply matches nothing.
inline constexpr Resource kResources[] = {
    {"newtab.html", "text/html"},
    {"unreachable.html", "text/html"},
    {"topbar.html", "text/html"},
    {"sidebar.html", "text/html"},
    {"settings.html", "text/html"},
    {"history.html", "text/html"},
    {"downloads.html", "text/html"},
    {"shell.css", "text/css"},
    {"shell.js", "text/javascript"},
    {"newtab.css", "text/css"},
    {"topbar.css", "text/css"},
    {"sidebar.css", "text/css"},
    {"unreachable.css", "text/css"},
    {"internal.css", "text/css"},
    {"logo.svg", "image/svg+xml"},
    // Bundled, never fetched. A Google Fonts <link> would make Frame's own new
    // tab page phone home to a third party on every open, which is the exact
    // behaviour this browser exists to avoid. SIL OFL 1.1; the licence travels
    // with the file as space-grotesk-OFL.txt.
    {"space-grotesk.woff2", "font/woff2"},
};

/// Turns a frame:// host and path into the resource name to serve.
///
///   frame://newtab            -> "newtab.html"
///   frame://newtab/shell.css  -> "shell.css"
///
/// Returns the raw candidate name; it still has to survive Find().
inline std::string ResolveName(const std::string& host,
                               const std::string& path) {
  std::string trimmed = path;
  while (!trimmed.empty() && trimmed.front() == '/') {
    trimmed.erase(trimmed.begin());
  }
  if (trimmed.empty()) {
    return host + ".html";
  }
  return trimmed;
}

/// The allowlist lookup. Anything not named exactly is refused, which includes
/// every form of traversal, absolute path, and alternate separator.
inline const Resource* Find(const std::string& name) {
  if (name.empty()) {
    return nullptr;
  }
  // A name is a bare filename or nothing. Rejecting separators and dot-dot
  // explicitly means a mistake in the table cannot become a traversal.
  if (name.find('/') != std::string::npos ||
      name.find('\\') != std::string::npos ||
      name.find("..") != std::string::npos ||
      name.find(':') != std::string::npos) {
    return nullptr;
  }
  for (const Resource& resource : kResources) {
    if (name == resource.name) {
      return &resource;
    }
  }
  return nullptr;
}

/// Convenience: resolve and look up in one step.
inline const Resource* Lookup(const std::string& host,
                              const std::string& path) {
  return Find(ResolveName(host, path));
}

}  // namespace frame::internal_pages

#endif  // FRAME_SHARED_INTERNAL_PAGES_H_
