#include "browser/browsing_data.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "browser/favorites.h"  // ProfileDir
#include "shared/url_util.h"

namespace frame {
namespace {

// Tab-separated records mean a tab inside a value would split it in two, and a
// newline would forge a whole record. Both come from page titles, which are
// hostile input — so both are escaped, along with the backslash that does the
// escaping.
std::string EscapeField(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (char ch : value) {
    switch (ch) {
      case '\\': out += "\\\\"; break;
      case '\t': out += "\\t"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      default: out += ch; break;
    }
  }
  return out;
}

std::string UnescapeField(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] != '\\' || i + 1 >= value.size()) {
      out += value[i];
      continue;
    }
    switch (value[++i]) {
      case 't': out += '\t'; break;
      case 'n': out += '\n'; break;
      case 'r': out += '\r'; break;
      case '\\': out += '\\'; break;
      default: out += value[i]; break;
    }
  }
  return out;
}

std::vector<std::string> SplitFields(const std::string& line, size_t expected) {
  std::vector<std::string> fields;
  std::string current;
  for (char ch : line) {
    if (ch == '\t' && fields.size() + 1 < expected) {
      fields.push_back(current);
      current.clear();
      continue;
    }
    current += ch;
  }
  fields.push_back(current);
  return fields;
}

std::string LowerAscii(const std::string& value) {
  std::string out = value;
  for (char& ch : out) {
    if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch - 'A' + 'a');
    }
  }
  return out;
}

// A cap, not a policy.
//
// The file is loaded whole and searched linearly, which is the right shape for
// something a person is meant to be able to read — but it is not the right
// shape for an unbounded one. 50k entries is years of browsing and about 5MB.
constexpr size_t kMaxHistoryEntries = 50000;

}  // namespace

long long WallClockMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch())
      .count();
}

// --- HistoryStore ---------------------------------------------------------

HistoryStore::HistoryStore(const std::string& path) : path_(path) {}

void HistoryStore::Load() {
  entries_.clear();
  std::ifstream file(path_);
  if (!file) {
    return;
  }
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty()) {
      continue;
    }
    const std::vector<std::string> fields = SplitFields(line, 4);
    if (fields.size() < 4) {
      continue;  // A truncated line is skipped, never guessed at.
    }
    HistoryEntry entry;
    entry.visited_at_ms = std::atoll(fields[0].c_str());
    entry.visit_count = std::max(1, std::atoi(fields[1].c_str()));
    entry.url = UnescapeField(fields[2]);
    entry.title = UnescapeField(fields[3]);
    if (!entry.url.empty()) {
      entries_.push_back(entry);
    }
  }
}

void HistoryStore::Save() const {
  // Written to a temporary and moved into place. A crash or a full disk
  // half-way through a direct write leaves the whole history truncated, and
  // this is the one file whose loss the user cannot undo.
  const std::string temp = path_ + ".tmp";
  {
    std::ofstream file(temp, std::ios::trunc);
    if (!file) {
      return;
    }
    for (const HistoryEntry& entry : entries_) {
      file << entry.visited_at_ms << '\t' << entry.visit_count << '\t'
           << EscapeField(entry.url) << '\t' << EscapeField(entry.title)
           << '\n';
    }
  }
  std::error_code ec;
  std::filesystem::rename(temp, path_, ec);
  if (ec) {
    // A rename across a locked target can fail on Windows; falling back to a
    // copy is still better than losing the write entirely.
    std::filesystem::copy_file(
        temp, path_, std::filesystem::copy_options::overwrite_existing, ec);
    std::filesystem::remove(temp, ec);
  }
}

void HistoryStore::Record(const std::string& url, const std::string& title) {
  // frame:// pages are the browser's own UI, not places the user went. About
  // and blank pages likewise. Recording them buries real history under noise.
  if (url.empty() || !url::StartsWith(url, "http")) {
    return;
  }

  // A page that rewrites its own URL — every client-side router does — would
  // otherwise write a record per interaction. Collapsing onto the most recent
  // entry for the same URL is what keeps that to one line.
  if (!entries_.empty() && entries_.back().url == url) {
    entries_.back().visited_at_ms = WallClockMs();
    if (!title.empty()) {
      entries_.back().title = title;
    }
    ++entries_.back().visit_count;
    Save();
    return;
  }

  HistoryEntry entry;

  entry.visited_at_ms = WallClockMs();
  entry.url = url;
  entry.title = title.empty() ? url : title;
  entries_.push_back(entry);

  if (entries_.size() > kMaxHistoryEntries) {
    entries_.erase(entries_.begin(),
                   entries_.begin() + (entries_.size() - kMaxHistoryEntries));
  }
  Save();
}

void HistoryStore::UpdateTitle(const std::string& url,
                               const std::string& title) {
  if (url.empty() || !url::StartsWith(url, "http")) {
    return;
  }
  if (!entries_.empty() && entries_.back().url == url) {
    // The common case: a title arriving for the page just navigated to. The
    // count is deliberately untouched — this is the same visit, described
    // better, not another one.
    if (!title.empty() && entries_.back().title != title) {
      entries_.back().title = title;
      Save();
    }
    return;
  }
  // Not the most recent entry, which means no address change was seen for it —
  // a tab opened directly at a URL is created already pointing there, so the
  // only signal that it was visited is its title arriving.
  Record(url, title);
}

std::vector<HistoryEntry> HistoryStore::Search(const std::string& query,
                                               size_t limit) const {
  const std::string needle = LowerAscii(query);
  std::vector<HistoryEntry> found;
  // Backwards: newest first is the order the page shows, and it means a search
  // that hits its limit returns the most recent matches rather than the oldest.
  for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
    if (found.size() >= limit) {
      break;
    }
    if (!needle.empty() &&
        LowerAscii(it->url).find(needle) == std::string::npos &&
        LowerAscii(it->title).find(needle) == std::string::npos) {
      continue;
    }
    found.push_back(*it);
  }
  return found;
}

bool HistoryStore::Remove(const std::string& url) {
  const size_t before = entries_.size();
  entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                [&url](const HistoryEntry& entry) {
                                  return entry.url == url;
                                }),
                 entries_.end());
  if (entries_.size() == before) {
    return false;
  }
  Save();
  return true;
}

size_t HistoryStore::ClearSince(long long since_ms) {
  const size_t before = entries_.size();
  entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                [since_ms](const HistoryEntry& entry) {
                                  return entry.visited_at_ms >= since_ms;
                                }),
                 entries_.end());
  const size_t removed = before - entries_.size();
  if (removed) {
    Save();
  }
  return removed;
}

// --- DownloadStore --------------------------------------------------------

DownloadStore::DownloadStore(const std::string& path) : path_(path) {}

void DownloadStore::Load() {
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
    const std::vector<std::string> fields = SplitFields(line, 6);
    if (fields.size() < 6) {
      continue;
    }
    DownloadRecord record;
    record.started_at_ms = std::atoll(fields[0].c_str());
    record.received_bytes = std::atoll(fields[1].c_str());
    record.total_bytes = std::atoll(fields[2].c_str());
    record.state = UnescapeField(fields[3]);
    record.url = UnescapeField(fields[4]);
    record.path = UnescapeField(fields[5]);

    // A download that was in flight when the browser stopped did not finish,
    // and saying otherwise on the next launch would be a lie the page repeats
    // forever.
    if (record.state == "in-progress") {
      record.state = "interrupted";
    }

    const size_t slash = record.path.find_last_of("\\/");
    record.filename = slash == std::string::npos ? record.path
                                                 : record.path.substr(slash + 1);
    if (!record.path.empty()) {
      items_.push_back(record);
    }
  }
}

void DownloadStore::Save() const {
  std::ofstream file(path_, std::ios::trunc);
  if (!file) {
    return;
  }
  for (const DownloadRecord& item : items_) {
    file << item.started_at_ms << '\t' << item.received_bytes << '\t'
         << item.total_bytes << '\t' << EscapeField(item.state) << '\t'
         << EscapeField(item.url) << '\t' << EscapeField(item.path) << '\n';
  }
}

DownloadRecord* DownloadStore::Find(uint32_t id) {
  for (DownloadRecord& item : items_) {
    if (item.id == id && id != 0) {
      return &item;
    }
  }
  return nullptr;
}

void DownloadStore::Upsert(const DownloadRecord& record) {
  for (DownloadRecord& item : items_) {
    // Matched on the id while in flight, and on the path once it is only a
    // record — an id is meaningless across a restart.
    const bool same = (record.id != 0 && item.id == record.id) ||
                      (record.id == 0 && item.path == record.path);
    if (!same) {
      continue;
    }
    const long long started = item.started_at_ms;
    item = record;
    item.started_at_ms = started;  // Never moves once set.
    Save();
    return;
  }
  items_.insert(items_.begin(), record);
  Save();
}

bool DownloadStore::Remove(const std::string& path) {
  const size_t before = items_.size();
  items_.erase(std::remove_if(items_.begin(), items_.end(),
                              [&path](const DownloadRecord& item) {
                                return item.path == path;
                              }),
               items_.end());
  if (items_.size() == before) {
    return false;
  }
  Save();
  return true;
}

void DownloadStore::ClearFinished() {
  items_.erase(std::remove_if(items_.begin(), items_.end(),
                              [](const DownloadRecord& item) {
                                return item.state != "in-progress";
                              }),
               items_.end());
  Save();
}

void DownloadStore::ClearAll() {
  // Records only. The files stay where they were saved — they belong to the
  // user, and clearing a list is not a request to delete them.
  items_.clear();
  Save();
}

// --- SettingsStore --------------------------------------------------------

SettingsStore::SettingsStore(const std::string& path) : path_(path) {}

void SettingsStore::Load() {
  values_.clear();
  std::ifstream file(path_);
  if (!file) {
    return;
  }
  std::string line;
  while (std::getline(file, line)) {
    const size_t tab = line.find('\t');
    if (tab == std::string::npos) {
      continue;
    }
    values_.emplace_back(line.substr(0, tab), UnescapeField(line.substr(tab + 1)));
  }
}

void SettingsStore::Save() const {
  std::ofstream file(path_, std::ios::trunc);
  if (!file) {
    return;
  }
  for (const auto& entry : values_) {
    file << entry.first << '\t' << EscapeField(entry.second) << '\n';
  }
}

const std::string* SettingsStore::Lookup(const std::string& key) const {
  for (const auto& entry : values_) {
    if (entry.first == key) {
      return &entry.second;
    }
  }
  return nullptr;
}

bool SettingsStore::Has(const std::string& key) const {
  return Lookup(key) != nullptr;
}

bool SettingsStore::GetBool(const std::string& key, bool fallback) const {
  const std::string* value = Lookup(key);
  if (!value) {
    return fallback;
  }
  return *value == "1" || *value == "true";
}

long long SettingsStore::GetInt(const std::string& key,
                                long long fallback) const {
  const std::string* value = Lookup(key);
  return value ? std::atoll(value->c_str()) : fallback;
}

std::string SettingsStore::GetString(const std::string& key,
                                     const std::string& fallback) const {
  const std::string* value = Lookup(key);
  return value ? *value : fallback;
}

void SettingsStore::SetString(const std::string& key,
                              const std::string& value) {
  for (auto& entry : values_) {
    if (entry.first == key) {
      entry.second = value;
      Save();
      return;
    }
  }
  values_.emplace_back(key, value);
  Save();
}

void SettingsStore::SetBool(const std::string& key, bool value) {
  SetString(key, value ? "1" : "0");
}

void SettingsStore::SetInt(const std::string& key, long long value) {
  SetString(key, std::to_string(value));
}

std::string SettingsStore::ToJson() const {
  std::ostringstream json;
  json << '{';
  for (size_t i = 0; i < values_.size(); ++i) {
    if (i) {
      json << ',';
    }
    json << '"' << url::JsonEscape(values_[i].first) << "\":\""
         << url::JsonEscape(values_[i].second) << '"';
  }
  json << '}';
  return json.str();
}

// --- instances ------------------------------------------------------------

HistoryStore& History() {
  static HistoryStore store(ProfileDir() + "\\history.tsv");
  static bool loaded = false;
  if (!loaded) {
    loaded = true;
    store.Load();
  }
  return store;
}

DownloadStore& Downloads() {
  static DownloadStore store(ProfileDir() + "\\downloads.tsv");
  static bool loaded = false;
  if (!loaded) {
    loaded = true;
    store.Load();
  }
  return store;
}

SettingsStore& Settings() {
  static SettingsStore store(ProfileDir() + "\\settings.tsv");
  static bool loaded = false;
  if (!loaded) {
    loaded = true;
    store.Load();
  }
  return store;
}

}  // namespace frame
