#include "browser/content_filter.h"

#include <atomic>
#include <fstream>
#include <string>

namespace frame::content_filter {
namespace {

// Function-local statics rather than globals: initialisation order across
// translation units is not something to gamble a network hook on.
filter::Engine& MutableEngine() {
  static filter::Engine engine;
  return engine;
}

std::atomic<std::size_t>& Counter() {
  static std::atomic<std::size_t> blocked{0};
  return blocked;
}

// A deliberately small starting set, written out on first run so Frame blocks
// something meaningful before anyone has fetched a real list.
//
// This is NOT meant to be the product. It is the handful of domains that carry
// the majority of cross-site tracking, chosen so the feature is demonstrably
// working; scripts/get-filters.ps1 replaces it with EasyPrivacy and EasyList,
// which are maintained by people who do this full time. The file says so.
const char* const kStarterList[] = {
    "! Frame starter filter list.",
    "!",
    "! Replace this with a maintained list:",
    "!   powershell -File scripts\\get-filters.ps1",
    "! which fetches EasyPrivacy + EasyList into this same file.",
    "!",
    "! Syntax is a subset of Adblock Plus: ||host^ blocks a host and its",
    "! subdomains, $third-party restricts it to cross-site requests, and",
    "! @@ marks an exception. Cosmetic (##) rules are ignored by design.",
    "",
    "! --- advertising ---",
    "||doubleclick.net^",
    "||googlesyndication.com^",
    "||googleadservices.com^",
    "||adservice.google.com^",
    "||amazon-adsystem.com^",
    "||adnxs.com^",
    "||rubiconproject.com^",
    "||pubmatic.com^",
    "||openx.net^",
    "||criteo.com^",
    "||criteo.net^",
    "||taboola.com^",
    "||outbrain.com^",
    "||casalemedia.com^",
    "||smartadserver.com^",
    "",
    "! --- analytics and cross-site measurement ---",
    "||google-analytics.com^",
    "||googletagmanager.com^",
    "||scorecardresearch.com^",
    "||quantserve.com^",
    "||hotjar.com^$third-party",
    "||mixpanel.com^$third-party",
    "||segment.io^$third-party",
    "||segment.com^$third-party",
    "||amplitude.com^$third-party",
    "||fullstory.com^$third-party",
    "||mouseflow.com^$third-party",
    "||clarity.ms^$third-party",
    "||branch.io^$third-party",
    "",
    "! --- social widgets used as trackers off-site ---",
    "||connect.facebook.net^$third-party",
    "||facebook.com^$third-party",
    "||ads-twitter.com^",
    "||analytics.tiktok.com^",
    "",
    "! --- known fingerprinting / device-ID services ---",
    "||fingerprintjs.com^",
    "||fpjs.io^",
    "||deviceatlas.com^",
    "",
    "! --- exceptions: these are load-bearing for real sites ---",
    "@@||googlevideo.com^",
    "@@||ytimg.com^",
    "@@||gstatic.com^",
};

void WriteStarterList(const std::string& path) {
  std::ofstream out(path, std::ios::trunc);
  if (!out) {
    return;
  }
  for (const char* line : kStarterList) {
    out << line << "\n";
  }
}

}  // namespace

std::size_t Load(const std::string& profile_dir) {
  const std::string path = profile_dir + "\\filters.txt";

  std::ifstream probe(path);
  if (!probe) {
    probe.close();
    WriteStarterList(path);
  } else {
    probe.close();
  }

  std::ifstream in(path);
  if (!in) {
    return 0;  // No list is a reason to block nothing, never to fail startup.
  }

  filter::Engine& engine = MutableEngine();
  std::string line;
  std::size_t accepted = 0;
  while (std::getline(in, line)) {
    // Lists are distributed with CRLF as often as not.
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (engine.AddRule(line)) {
      ++accepted;
    }
  }
  return accepted;
}

const filter::Engine& Get() {
  return MutableEngine();
}

// The on/off switch, as an ATOMIC GATE rather than a mutable engine.
//
// The engine itself is deliberately immutable after startup — see the note in
// the header: it is read from the IO thread on every request, so anything that
// could rewrite it would be a data race per resource load. Clearing the rules
// to "turn blocking off" would be exactly that.
//
// A bool checked before the engine is consulted has the same effect and none
// of the hazard: the writer is the UI thread, the readers are IO threads, and
// a request that straddles the flip is blocked or not blocked by whichever
// value it happened to read. Both answers are correct for that instant.
std::atomic<bool>& EnabledFlag() {
  static std::atomic<bool> enabled{true};
  return enabled;
}

bool enabled() {
  return EnabledFlag().load(std::memory_order_relaxed);
}

void set_enabled(bool value) {
  EnabledFlag().store(value, std::memory_order_relaxed);
}

std::size_t total_blocked() {
  return Counter().load(std::memory_order_relaxed);
}

void note_blocked() {
  Counter().fetch_add(1, std::memory_order_relaxed);
}

}  // namespace frame::content_filter
