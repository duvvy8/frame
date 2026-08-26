// Frame — the stores behind the internal pages.
//
// frame://history, frame://downloads and frame://settings used to be static
// documents explaining what would eventually go there. These are the things
// that had to exist first: real records, on disk, that the pages read and
// write.
//
// PROCESS MODEL: browser process, CEF UI thread only. Everything here is
// touched from window callbacks and message-router handlers, all of which run
// on that one thread. No locks, and none should be added on the assumption
// that a singleton is shared across threads — it is not.
//
// FILE FORMAT: one record per line, tab-separated, newest last. Deliberately
// not JSON or SQLite. The user is meant to be able to open these files and see
// what the browser has kept about them, and delete a line — a privacy claim is
// worth more when it is checkable. Tabs and newlines are escaped on write, so
// a title containing either cannot forge a record.

#ifndef FRAME_BROWSER_BROWSING_DATA_H_
#define FRAME_BROWSER_BROWSING_DATA_H_

#include <cstdint>
#include <string>
#include <vector>

namespace frame {

// --- history --------------------------------------------------------------

struct HistoryEntry {
  // Milliseconds since the Unix epoch. Wall clock, not a steady clock: this is
  // shown to a person as a date, so it has to follow the system clock.
  long long visited_at_ms = 0;
  std::string url;
  std::string title;
  // How many times this URL has been visited. Repeated visits update the
  // existing record rather than appending, so a page refreshed forty times is
  // one line in the file and one row in the list.
  int visit_count = 1;
};

class HistoryStore {
 public:
  explicit HistoryStore(const std::string& path);

  void Load();
  void Save() const;

  // A NAVIGATION happened. Appends an entry, or bumps the count of the one
  // already at the top if it is the same URL — a reload is a visit, and a page
  // that rewrites its own location is not a hundred of them.
  void Record(const std::string& url, const std::string& title);

  // A TITLE arrived for a page already visited.
  //
  // Separate from Record on purpose. One navigation produces both an address
  // change and a title change, and routing both through Record counted every
  // visit three times — the history said "3×" for a page opened once, which is
  // worse than no count at all. This updates without counting, and falls back
  // to recording a visit if the URL is not the most recent entry (a tab opened
  // directly at a URL never reports an address change for it).
  void UpdateTitle(const std::string& url, const std::string& title);

  // Newest first, optionally filtered. The query matches the URL or the title,
  // case-insensitively, as a substring.
  std::vector<HistoryEntry> Search(const std::string& query, size_t limit) const;

  size_t size() const { return entries_.size(); }

  // Removes one entry by URL. Returns whether anything matched.
  bool Remove(const std::string& url);

  // Removes everything visited at or after `since_ms`. Zero clears the lot,
  // which is what "all time" means on the page.
  size_t ClearSince(long long since_ms);

 private:
  std::string path_;
  // Oldest first, so appending is the common case and the file reads
  // chronologically.
  std::vector<HistoryEntry> entries_;
};

// --- downloads ------------------------------------------------------------

struct DownloadRecord {
  // CEF's own id for the item while it is in flight. Zero once it is only a
  // record on disk.
  uint32_t id = 0;
  std::string url;
  std::string path;
  std::string filename;
  long long started_at_ms = 0;
  int64_t received_bytes = 0;
  int64_t total_bytes = 0;
  // "in-progress" | "complete" | "cancelled" | "interrupted"
  std::string state = "in-progress";
};

class DownloadStore {
 public:
  explicit DownloadStore(const std::string& path);

  void Load();
  void Save() const;

  // Creates or updates the record for a CEF download item.
  void Upsert(const DownloadRecord& record);

  // Newest first.
  const std::vector<DownloadRecord>& items() const { return items_; }

  DownloadRecord* Find(uint32_t id);
  bool Remove(const std::string& path);
  void ClearFinished();

  // Only records are cleared, never files. A download that has landed is the
  // user's file and deleting it because they tidied a list would be a
  // spectacular thing to get wrong.
  void ClearAll();

 private:
  std::string path_;
  std::vector<DownloadRecord> items_;
};

// --- settings -------------------------------------------------------------

// A flat string map, persisted as key<TAB>value lines.
//
// Typed accessors rather than raw strings at the call sites: a setting read as
// a bool in one place and a string in another is how a settings system starts
// disagreeing with itself.
class SettingsStore {
 public:
  explicit SettingsStore(const std::string& path);

  void Load();
  void Save() const;

  bool GetBool(const std::string& key, bool fallback) const;
  long long GetInt(const std::string& key, long long fallback) const;
  std::string GetString(const std::string& key,
                        const std::string& fallback) const;

  void SetBool(const std::string& key, bool value);
  void SetInt(const std::string& key, long long value);
  void SetString(const std::string& key, const std::string& value);

  bool Has(const std::string& key) const;

  // Every stored key, as JSON. What the settings page reads to render itself,
  // so the page never holds a second copy of the defaults.
  std::string ToJson() const;

 private:
  std::string path_;
  std::vector<std::pair<std::string, std::string>> values_;

  const std::string* Lookup(const std::string& key) const;
};

// --- the process-wide instances -------------------------------------------
//
// One of each, created on first use under the profile directory. Frame can
// have several windows and they must not each keep their own history.
HistoryStore& History();
DownloadStore& Downloads();
SettingsStore& Settings();

// Milliseconds since the Unix epoch, for the timestamps above.
long long WallClockMs();

}  // namespace frame

#endif  // FRAME_BROWSER_BROWSING_DATA_H_
