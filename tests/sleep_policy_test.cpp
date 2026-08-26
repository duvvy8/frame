// Tests for shared/sleep_policy.h.
//
// This is the header that decides whether a tab is discarded, and discarding a
// tab throws away whatever the page was holding — scroll position, form
// contents, everything the renderer had in memory. The failure mode is not a
// crash, it is a user coming back to a tab and finding what they typed is
// gone. So the interesting cases here are all the ones where the answer must
// be NO.

#include <catch2/catch_test_macros.hpp>

#include "shared/sleep_policy.h"

using frame::sleep::Blocker;
using frame::sleep::ClampIdleMs;
using frame::sleep::FirstBlocker;
using frame::sleep::IsSleepableUrl;
using frame::sleep::MaySleep;
using frame::sleep::Settings;
using frame::sleep::TabFacts;

namespace {

constexpr unsigned long long kMinute = 60ULL * 1000ULL;
constexpr unsigned long long kNow = 10ULL * 60ULL * 60ULL * 1000ULL;  // 10h

// A tab that is safe to sleep in every respect. Each test then spoils exactly
// one thing about it, so a failure names the rule that broke rather than
// leaving the whole struct under suspicion.
TabFacts SleepableTab() {
  TabFacts tab;
  tab.active = false;
  tab.asleep = false;
  tab.never_sleep = false;
  tab.loading = false;
  tab.audible = false;
  tab.has_unsaved_input = false;
  tab.url = "https://example.com/article";
  tab.backgrounded_at_ms = kNow - 60ULL * kMinute;
  return tab;
}

Settings DefaultSettings() {
  Settings settings;
  settings.enabled = true;
  settings.idle_ms = 30ULL * kMinute;
  return settings;
}

}  // namespace

TEST_CASE("A long-idle background tab may sleep", "[sleep]") {
  CHECK(MaySleep(SleepableTab(), DefaultSettings(), kNow));
  CHECK(FirstBlocker(SleepableTab(), DefaultSettings(), kNow) == Blocker::kNone);
}

TEST_CASE("The tab being looked at is never slept", "[sleep]") {
  TabFacts tab = SleepableTab();
  tab.active = true;
  CHECK(FirstBlocker(tab, DefaultSettings(), kNow) == Blocker::kActive);
}

TEST_CASE("Unsaved input outranks any amount of idle time", "[sleep]") {
  TabFacts tab = SleepableTab();
  tab.has_unsaved_input = true;
  // A week idle, and it still must not be discarded: the text in that form is
  // not recoverable and no idle threshold makes losing it acceptable.
  tab.backgrounded_at_ms = 0 + 1;
  CHECK(FirstBlocker(tab, DefaultSettings(), kNow) == Blocker::kUnsavedInput);
  CHECK_FALSE(MaySleep(tab, DefaultSettings(), kNow));
}

TEST_CASE("Audible tabs stay awake", "[sleep]") {
  TabFacts tab = SleepableTab();
  tab.audible = true;
  CHECK(FirstBlocker(tab, DefaultSettings(), kNow) == Blocker::kAudible);
}

TEST_CASE("A loading tab is not discarded mid-load", "[sleep]") {
  TabFacts tab = SleepableTab();
  tab.loading = true;
  CHECK(FirstBlocker(tab, DefaultSettings(), kNow) == Blocker::kLoading);
}

TEST_CASE("The per-tab opt-out is honoured", "[sleep]") {
  TabFacts tab = SleepableTab();
  tab.never_sleep = true;
  CHECK(FirstBlocker(tab, DefaultSettings(), kNow) == Blocker::kNeverSleep);
}

TEST_CASE("Only http(s) pages sleep", "[sleep]") {
  CHECK(IsSleepableUrl("http://example.com"));
  CHECK(IsSleepableUrl("https://example.com"));

  // Everything else has nothing worth reclaiming, nothing to restore from, or
  // both.
  CHECK_FALSE(IsSleepableUrl("frame://newtab"));
  CHECK_FALSE(IsSleepableUrl("about:blank"));
  CHECK_FALSE(IsSleepableUrl("data:text/html,hi"));
  CHECK_FALSE(IsSleepableUrl("file:///C:/notes.txt"));
  CHECK_FALSE(IsSleepableUrl(""));

  // Near-misses that must NOT be treated as http.
  CHECK_FALSE(IsSleepableUrl("https:/example.com"));
  CHECK_FALSE(IsSleepableUrl("xhttp://example.com"));

  TabFacts tab = SleepableTab();
  tab.url = "frame://settings";
  CHECK(FirstBlocker(tab, DefaultSettings(), kNow) == Blocker::kInternalPage);
}

TEST_CASE("Idle time is measured, not assumed", "[sleep]") {
  Settings settings = DefaultSettings();
  settings.idle_ms = 30ULL * kMinute;

  TabFacts tab = SleepableTab();

  tab.backgrounded_at_ms = kNow - 29ULL * kMinute;
  CHECK(FirstBlocker(tab, settings, kNow) == Blocker::kNotIdleYet);

  tab.backgrounded_at_ms = kNow - 30ULL * kMinute;
  CHECK(FirstBlocker(tab, settings, kNow) == Blocker::kNone);

  tab.backgrounded_at_ms = kNow - 31ULL * kMinute;
  CHECK(FirstBlocker(tab, settings, kNow) == Blocker::kNone);
}

TEST_CASE("A tab that has never been backgrounded is not idle", "[sleep]") {
  TabFacts tab = SleepableTab();
  // Zero means "never", not "idle since the epoch" — which is the reading that
  // would make every freshly opened tab instantly sleepable.
  tab.backgrounded_at_ms = 0;
  CHECK(FirstBlocker(tab, DefaultSettings(), kNow) == Blocker::kNotIdleYet);
}

TEST_CASE("A clock that goes backwards does not mean infinitely idle",
          "[sleep]") {
  TabFacts tab = SleepableTab();
  // Not reachable from a steady clock, but the subtraction is unsigned: read
  // naively, a timestamp in the future underflows to eighteen quintillion
  // milliseconds of idle time and the tab is discarded instantly.
  tab.backgrounded_at_ms = kNow + 5ULL * kMinute;
  CHECK(FirstBlocker(tab, DefaultSettings(), kNow) == Blocker::kNotIdleYet);
}

TEST_CASE("Sleeping switched off blocks everything", "[sleep]") {
  Settings settings = DefaultSettings();
  settings.enabled = false;
  CHECK(FirstBlocker(SleepableTab(), settings, kNow) == Blocker::kDisabled);
}

TEST_CASE("An already-sleeping tab is not slept again", "[sleep]") {
  TabFacts tab = SleepableTab();
  tab.asleep = true;
  CHECK(FirstBlocker(tab, DefaultSettings(), kNow) == Blocker::kAlreadyAsleep);
}

TEST_CASE("The idle bound rejects values that would sleep everything",
          "[sleep]") {
  // Zero is reachable from a settings file by hand, and would mean "discard
  // every background tab immediately".
  CHECK(ClampIdleMs(0) == frame::sleep::kMinIdleMs);
  CHECK(ClampIdleMs(1) == frame::sleep::kMinIdleMs);
  CHECK(ClampIdleMs(30ULL * kMinute) == 30ULL * kMinute);
  CHECK(ClampIdleMs(~0ULL) == frame::sleep::kMaxIdleMs);
}

TEST_CASE("Blockers are reported in priority order", "[sleep]") {
  // A tab can be several kinds of ineligible at once. Which one is reported
  // decides what the UI says, so the order is part of the contract.
  TabFacts tab = SleepableTab();
  tab.active = true;
  tab.audible = true;
  tab.has_unsaved_input = true;
  CHECK(FirstBlocker(tab, DefaultSettings(), kNow) == Blocker::kActive);

  tab.active = false;
  CHECK(FirstBlocker(tab, DefaultSettings(), kNow) == Blocker::kAudible);

  tab.audible = false;
  CHECK(FirstBlocker(tab, DefaultSettings(), kNow) == Blocker::kUnsavedInput);
}
