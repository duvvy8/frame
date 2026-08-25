#include "browser/favorites.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "include/cef_parser.h"
#include "include/cef_urlrequest.h"

namespace frame {
namespace {

// Field separator for the on-disk list. Tab rather than comma because a title
// can contain almost anything except a tab.
const char kSeparator = '\t';

std::string MimeForIcon(const std::string& bytes) {
  if (bytes.size() > 8 && static_cast<unsigned char>(bytes[0]) == 0x89 &&
      bytes.compare(1, 3, "PNG") == 0) {
    return "image/png";
  }
  if (bytes.size() > 3 && bytes.compare(0, 4, "<svg") == 0) {
    return "image/svg+xml";
  }
  if (bytes.size() > 2 && static_cast<unsigned char>(bytes[0]) == 0xFF &&
      static_cast<unsigned char>(bytes[1]) == 0xD8) {
    return "image/jpeg";
  }
  // ICO is the common case and the default a bare /favicon.ico returns.
  return "image/x-icon";
}

// Downloads one icon and hands the bytes back. Kept private to this file
// because nothing else should be making network requests on its own.
class IconRequestClient : public CefURLRequestClient {
 public:
  using Done = std::function<void(const std::string&)>;

  explicit IconRequestClient(Done done) : done_(std::move(done)) {}

  void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
    const bool ok = request->GetRequestStatus() == UR_SUCCESS &&
                    request->GetResponse() &&
                    request->GetResponse()->GetStatus() == 200;
    // An icon that is implausibly large is not an icon; refusing it keeps a
    // hostile or misconfigured server from parking megabytes in the cache.
    if (ok && !buffer_.empty() && buffer_.size() <= 256 * 1024) {
      done_(buffer_);
    } else {
      done_(std::string());
    }
  }

  void OnDownloadData(CefRefPtr<CefURLRequest> request,
                      const void* data,
                      size_t data_length) override {
    if (buffer_.size() + data_length > 1024 * 1024) {
      return;  // Hard ceiling regardless of what the server claims.
    }
    buffer_.append(static_cast<const char*>(data), data_length);
  }

  void OnUploadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}
  void OnDownloadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}
  bool GetAuthCredentials(bool,
                          const CefString&,
                          int,
                          const CefString&,
                          const CefString&,
                          CefRefPtr<CefAuthCallback>) override {
    return false;  // Never authenticate to fetch an icon.
  }

 private:
  Done done_;
  std::string buffer_;

  IMPLEMENT_REFCOUNTING(IconRequestClient);
  DISALLOW_COPY_AND_ASSIGN(IconRequestClient);
};

}  // namespace

std::string HostOf(const std::string& url) {
  CefURLParts parts;
  if (!CefParseURL(url, parts)) {
    return std::string();
  }
  return CefString(&parts.host).ToString();
}

std::string ProfileDir() {
  wchar_t* local = nullptr;
  size_t length = 0;
  std::wstring base;
  if (_wdupenv_s(&local, &length, L"LOCALAPPDATA") == 0 && local) {
    base = local;
    free(local);
  }
  if (base.empty()) {
    base = L".";
  }
  const std::wstring dir = base + L"\\Frame";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return CefString(dir).ToString();
}

// --- FaviconCache ---------------------------------------------------------

FaviconCache::FaviconCache(const std::string& directory)
    : directory_(directory) {
  std::error_code ec;
  std::filesystem::create_directories(directory_, ec);
}

void FaviconCache::Load() {
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(directory_, ec)) {
    if (ec || !entry.is_regular_file()) {
      continue;
    }
    std::ifstream file(entry.path(), std::ios::binary);
    if (!file) {
      continue;
    }
    const std::string bytes((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
    if (bytes.empty()) {
      continue;
    }
    const std::string host = entry.path().stem().string();
    data_urls_[host] = "data:" + MimeForIcon(bytes) + ";base64," +
                       CefBase64Encode(bytes.data(), bytes.size()).ToString();
  }
}

std::string FaviconCache::DataUrl(const std::string& host) const {
  const auto it = data_urls_.find(host);
  return it == data_urls_.end() ? std::string() : it->second;
}

void FaviconCache::Store(const std::string& host, const std::string& bytes) {
  data_urls_[host] = "data:" + MimeForIcon(bytes) + ";base64," +
                     CefBase64Encode(bytes.data(), bytes.size()).ToString();

  // Host names come from a parsed URL, but this one becomes a filename, so it
  // is reduced to characters that cannot mean anything to a filesystem.
  std::string safe;
  for (char ch : host) {
    safe.push_back((std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' ||
                    ch == '.')
                       ? ch
                       : '_');
  }
  std::ofstream file(directory_ + "\\" + safe + ".ico", std::ios::binary);
  if (file) {
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }
}

void FaviconCache::Fetch(const std::string& host,
                         const std::string& icon_url,
                         std::function<void()> on_ready) {
  if (host.empty() || icon_url.empty()) {
    return;
  }
  if (!DataUrl(host).empty() || in_flight_[host]) {
    return;  // Already have it, or already asking.
  }
  in_flight_[host] = true;

  CefRefPtr<CefRequest> request = CefRequest::Create();
  request->SetURL(icon_url);
  request->SetMethod("GET");
  // No referrer: fetching an icon should not tell the site which page
  // prompted it.
  request->SetFlags(UR_FLAG_NO_RETRY_ON_5XX | UR_FLAG_STOP_ON_REDIRECT);

  CefRefPtr<IconRequestClient> client(new IconRequestClient(
      [this, host, on_ready](const std::string& bytes) {
        in_flight_[host] = false;
        if (bytes.empty()) {
          return;
        }
        Store(host, bytes);
        if (on_ready) {
          on_ready();
        }
      }));

  CefURLRequest::Create(request, client, nullptr);
}

// --- FavoritesStore -------------------------------------------------------

FavoritesStore::FavoritesStore(const std::string& path) : path_(path) {}

void FavoritesStore::Load() {
  items_.clear();
  std::ifstream file(path_);
  if (!file) {
    return;
  }
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty()) {
      continue;
    }
    const size_t tab = line.find(kSeparator);
    if (tab == std::string::npos) {
      items_.push_back({line, line});
      continue;
    }
    items_.push_back({line.substr(0, tab), line.substr(tab + 1)});
  }
}

void FavoritesStore::Save() const {
  std::ofstream file(path_, std::ios::trunc);
  if (!file) {
    return;
  }
  for (const Favorite& item : items_) {
    file << item.url << kSeparator << item.title << "\n";
  }
}

void FavoritesStore::Add(const std::string& url, const std::string& title) {
  if (url.empty()) {
    return;
  }
  for (const Favorite& item : items_) {
    if (item.url == url) {
      return;  // Already pinned.
    }
  }
  items_.push_back({url, title.empty() ? HostOf(url) : title});
  Save();
}

void FavoritesStore::Remove(const std::string& url) {
  const auto it = std::remove_if(
      items_.begin(), items_.end(),
      [&url](const Favorite& item) { return item.url == url; });
  if (it != items_.end()) {
    items_.erase(it, items_.end());
    Save();
  }
}

void FavoritesStore::Move(int from, int to) {
  const int last = static_cast<int>(items_.size()) - 1;
  if (last < 0 || from < 0 || from > last) {
    return;
  }
  const int target = std::max(0, std::min(to, last));
  if (target == from) {
    return;
  }
  Favorite moved = items_[from];
  items_.erase(items_.begin() + from);
  items_.insert(items_.begin() + target, moved);
  Save();
}

void FavoritesStore::EnsureDefaults() {
  if (!items_.empty()) {
    return;
  }
  // A fresh profile starts with something rather than an empty grid. These are
  // ordinary pins the user can remove.
  items_ = {
      {"https://www.google.com", "Google"},
      {"https://www.youtube.com", "YouTube"},
      {"https://github.com", "GitHub"},
      {"https://en.wikipedia.org", "Wikipedia"},
  };
  Save();
}

}  // namespace frame
