#ifndef FRAME_BROWSER_FAVORITES_H_
#define FRAME_BROWSER_FAVORITES_H_

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "include/cef_base.h"

namespace frame {

// A pinned site in the sidebar.
struct Favorite {
  std::string url;
  std::string title;
};

// Favicons, fetched from the sites themselves and cached on disk.
//
// Deliberately NOT via a third-party favicon service. Those are the easy
// option, and they work by receiving every domain you have ever pinned —
// which is precisely the kind of quiet data flow this browser exists to avoid.
// A site is asked for its own icon, by the browser, once.
class FaviconCache {
 public:
  explicit FaviconCache(const std::string& directory);

  // Cached icon as a data: URL, or empty if there is not one yet. Data URLs
  // keep icons inside the state payload the chrome already receives, so no
  // extra scheme plumbing is needed to display them.
  std::string DataUrl(const std::string& host) const;

  // Downloads the icon for `host` if it is not already cached, invoking
  // `on_ready` once when something new lands.
  void Fetch(const std::string& host,
             const std::string& icon_url,
             std::function<void()> on_ready);

  void Load();

 private:
  void Store(const std::string& host, const std::string& bytes);

  std::string directory_;
  std::map<std::string, std::string> data_urls_;
  std::map<std::string, bool> in_flight_;
};

// The pinned sites, persisted so they survive a restart.
class FavoritesStore {
 public:
  explicit FavoritesStore(const std::string& path);

  void Load();
  void Save() const;

  const std::vector<Favorite>& items() const { return items_; }

  void Add(const std::string& url, const std::string& title);
  void Remove(const std::string& url);
  void Move(int from, int to);

  // Seeds a first run so the sidebar is not empty on a fresh profile.
  void EnsureDefaults();

 private:
  std::string path_;
  std::vector<Favorite> items_;
};

// The host part of a URL, which is the key everything here is stored under.
std::string HostOf(const std::string& url);

// Where Frame keeps its profile data.
std::string ProfileDir();

}  // namespace frame

#endif  // FRAME_BROWSER_FAVORITES_H_
