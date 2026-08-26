// Frame — when a tab may be put to sleep.
//
// Framework-free for the same reason chrome_layout.h and shortcuts.h are: this
// is the part that is easy to get quietly wrong, and getting it wrong destroys
// the user's work. A tab that sleeps while a form is half filled in loses what
// was typed, and it loses it invisibly — the tab looks fine, and the text is
// gone when they come back to it. That failure must be testable without a
// running browser.
//
// The shape follows Chromium's own TabLifecycleUnitSource, which is the
// reference for which tabs must never be discarded, plus Ember's per-tab
// opt-out. The policy answers "why is this tab still awake?" rather than
// yes/no, because the reason is worth showing in the UI and worth asserting on
// in a test.
//
// Note what is NOT here: nothing in this header knows about CEF, windows, or
// how sleeping is implemented. It is given facts and returns a verdict.

#ifndef FRAME_SHARED_SLEEP_POLICY_H_
#define FRAME_SHARED_SLEEP_POLICY_H_

#include <string>
#include <vector>

namespace frame::sleep {

// Why a tab is being kept awake. Ordered by how much they are worth reporting:
// the first blocker is the one to show.
enum class Blocker {
  kNone = 0,
  kDisabled,       // the feature is off
  kAlreadyAsleep,
  kActive,         // the tab the user is looking at
  kNeverSleep,     // an explicit per-tab opt-out
  kInternalPage,   // frame:// pages hold no renderer worth reclaiming
  kLoading,
  kAudible,        // playing sound, whether or not it is visible
  kUnsavedInput,   // a form with something typed into it
  kNotIdleYet,     // backgrounded, but not for long enough
};

// Everything the policy needs to know about one tab. Deliberately plain data:
// the caller gathers it, the policy judges it.
struct TabFacts {
  bool active = false;
  bool asleep = false;
  bool never_sleep = false;
  bool loading = false;
  bool audible = false;
  bool has_unsaved_input = false;
  std::string url;
  // Steady-clock milliseconds at which the tab stopped being active. Zero
  // means it has never been backgrounded, which is treated as "not idle" —
  // never as "idle since the epoch".
  unsigned long long backgrounded_at_ms = 0;
};

struct Settings {
  bool enabled = true;
  // How long a tab must sit in the background before it may sleep.
  unsigned long long idle_ms = 30ULL * 60ULL * 1000ULL;
};

// Only real web pages sleep.
//
// frame:// pages are a few kilobytes of local HTML with no meaningful renderer
// cost, and about: / data: URLs have nothing to restore from. Discarding them
// would cost the user the page and reclaim nothing, which is the wrong side of
// every trade this feature makes.
inline bool IsSleepableUrl(const std::string& url) {
  const auto starts_with = [&url](const char* prefix) {
    return url.rfind(prefix, 0) == 0;
  };
  return starts_with("http://") || starts_with("https://");
}

/// The first reason this tab must stay awake, or kNone if it may sleep.
///
/// Order matters: the checks that are cheap and absolute come first, and
/// kNotIdleYet comes last so a tab that is merely too recent reports that
/// rather than something more alarming.
inline Blocker FirstBlocker(const TabFacts& tab,
                            const Settings& settings,
                            unsigned long long now_ms) {
  if (!settings.enabled) {
    return Blocker::kDisabled;
  }
  if (tab.asleep) {
    return Blocker::kAlreadyAsleep;
  }
  if (tab.active) {
    return Blocker::kActive;
  }
  if (tab.never_sleep) {
    return Blocker::kNeverSleep;
  }
  if (!IsSleepableUrl(tab.url)) {
    return Blocker::kInternalPage;
  }
  if (tab.loading) {
    // Discarding mid-load throws away the work AND leaves the tab showing a
    // page it never finished, which is worse than either outcome alone.
    return Blocker::kLoading;
  }
  if (tab.audible) {
    return Blocker::kAudible;
  }
  if (tab.has_unsaved_input) {
    // The one blocker that exists to protect the USER rather than the browser.
    // A discarded tab cannot get typed text back, so this is never overridden
    // by memory pressure or by a long idle time.
    return Blocker::kUnsavedInput;
  }
  if (tab.backgrounded_at_ms == 0 ||
      now_ms < tab.backgrounded_at_ms ||
      now_ms - tab.backgrounded_at_ms < settings.idle_ms) {
    // now_ms < backgrounded_at_ms is not possible from a steady clock, but a
    // clock that has been observed to go backwards must not read as "idle for
    // eighteen quintillion milliseconds".
    return Blocker::kNotIdleYet;
  }
  return Blocker::kNone;
}

inline bool MaySleep(const TabFacts& tab,
                     const Settings& settings,
                     unsigned long long now_ms) {
  return FirstBlocker(tab, settings, now_ms) == Blocker::kNone;
}

/// A blocker as text, for the UI and for test failure messages.
inline const char* BlockerName(Blocker blocker) {
  switch (blocker) {
    case Blocker::kNone:          return "none";
    case Blocker::kDisabled:      return "disabled";
    case Blocker::kAlreadyAsleep: return "already asleep";
    case Blocker::kActive:        return "active";
    case Blocker::kNeverSleep:    return "never sleep";
    case Blocker::kInternalPage:  return "internal page";
    case Blocker::kLoading:       return "loading";
    case Blocker::kAudible:       return "playing audio";
    case Blocker::kUnsavedInput:  return "unsaved input";
    case Blocker::kNotIdleYet:    return "not idle yet";
  }
  return "unknown";
}

// Bounds on the configurable idle time, applied wherever a value arrives from
// outside — a settings file, a query string, a future sync. A zero or negative
// value would mean "sleep every background tab instantly", which is not a
// setting anyone wants and is trivially reachable by mistake.
inline constexpr unsigned long long kMinIdleMs = 60ULL * 1000ULL;             // 1 min
inline constexpr unsigned long long kMaxIdleMs = 12ULL * 60ULL * 60ULL * 1000ULL;  // 12 h

inline unsigned long long ClampIdleMs(unsigned long long value) {
  if (value < kMinIdleMs) {
    return kMinIdleMs;
  }
  if (value > kMaxIdleMs) {
    return kMaxIdleMs;
  }
  return value;
}

}  // namespace frame::sleep

#endif  // FRAME_SHARED_SLEEP_POLICY_H_
