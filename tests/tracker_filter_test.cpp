// Tests for the tracker filter engine.
//
// This is the component that decides whether a request leaves the machine, so
// the cases that matter most are the ones where being wrong is expensive:
// a rule that accidentally matches a parent domain takes out an entire site,
// and a malformed list line that becomes a rule could block everything.

#include <catch2/catch_test_macros.hpp>

#include "shared/tracker_filter.h"

namespace {

using frame::filter::Decision;
using frame::filter::Engine;
using frame::filter::IsThirdParty;
using frame::filter::RegistrableDomain;

Engine Built(std::initializer_list<const char*> lines) {
  Engine engine;
  for (const char* line : lines) {
    engine.AddRule(line);
  }
  return engine;
}

}  // namespace

TEST_CASE("Host extraction survives real URLs", "[filter][url]") {
  CHECK(Engine::HostOfUrl("https://ads.example.com/track.js") ==
        "ads.example.com");
  CHECK(Engine::HostOfUrl("http://example.com") == "example.com");
  CHECK(Engine::HostOfUrl("https://example.com:8443/a/b") == "example.com");
  CHECK(Engine::HostOfUrl("https://user:pw@example.com/x") == "example.com");
  CHECK(Engine::HostOfUrl("https://example.com?q=1") == "example.com");
  CHECK(Engine::HostOfUrl("https://example.com#frag") == "example.com");
  // A userinfo '@' AFTER the path must not be mistaken for credentials.
  CHECK(Engine::HostOfUrl("https://example.com/mail@host") == "example.com");
}

TEST_CASE("A rule covers subdomains but never parents", "[filter][matching]") {
  const Engine engine = Built({"||tracker.example^"});

  CHECK(engine.ShouldBlock("https://tracker.example/p.gif", "site.test"));
  CHECK(engine.ShouldBlock("https://a.tracker.example/p.gif", "site.test"));
  CHECK(engine.ShouldBlock("https://a.b.c.tracker.example/p", "site.test"));

  // The whole point: matching upward must not escape the rule's own domain.
  CHECK_FALSE(engine.ShouldBlock("https://example/p", "site.test"));
  CHECK_FALSE(engine.ShouldBlock("https://nottracker.example/p", "site.test"));
  CHECK_FALSE(engine.ShouldBlock("https://tracker.example.com/p", "site.test"));
  // A domain that merely ENDS with the rule text is a different site.
  CHECK_FALSE(engine.ShouldBlock("https://eviltracker.example/p", "site.test"));
}

TEST_CASE("Exceptions outrank blocks", "[filter][matching]") {
  const Engine engine = Built({"||example.com^", "@@||cdn.example.com^"});

  CHECK(engine.Classify("https://example.com/a", "site.test") ==
        Decision::kBlock);
  CHECK(engine.Classify("https://cdn.example.com/a", "site.test") ==
        Decision::kAllowException);
  // The exception is scoped to its own subtree, not to the parent rule.
  CHECK(engine.Classify("https://ads.example.com/a", "site.test") ==
        Decision::kBlock);
}

TEST_CASE("third-party rules stay off first-party requests",
          "[filter][matching]") {
  const Engine engine = Built({"||analytics.test^$third-party"});

  CHECK(engine.Classify("https://analytics.test/t.js", "shop.example") ==
        Decision::kBlock);
  // Same site loading its own analytics subdomain: left alone.
  CHECK(engine.Classify("https://analytics.test/t.js", "analytics.test") ==
        Decision::kAllowFirstParty);
  CHECK(engine.Classify("https://analytics.test/t.js", "www.analytics.test") ==
        Decision::kAllowFirstParty);
}

TEST_CASE("List syntax that should and should not become rules",
          "[filter][parsing]") {
  Engine engine;

  SECTION("accepted shapes") {
    CHECK(engine.AddRule("||doubleclick.net^"));
    CHECK(engine.AddRule("||scorecardresearch.com^$third-party"));
    CHECK(engine.AddRule("@@||googlevideo.com^"));
    CHECK(engine.AddRule("0.0.0.0 ads.example.org"));
    CHECK(engine.AddRule("127.0.0.1 beacon.example.org"));
    CHECK(engine.AddRule("plain-domain.example"));
    CHECK(engine.rule_count() == 6);
    CHECK(engine.exception_count() == 1);
  }

  SECTION("ignored shapes") {
    CHECK_FALSE(engine.AddRule(""));
    CHECK_FALSE(engine.AddRule("   "));
    CHECK_FALSE(engine.AddRule("! a comment"));
    CHECK_FALSE(engine.AddRule("[Adblock Plus 2.0]"));
    // Cosmetic rules are discarded: the request already happened.
    CHECK_FALSE(engine.AddRule("example.com##.ad-banner"));
    CHECK_FALSE(engine.AddRule("##.ad"));
    // Patterns host matching cannot answer.
    CHECK_FALSE(engine.AddRule("/ads/banner.gif"));
    CHECK_FALSE(engine.AddRule("||example.com/path/thing"));
    CHECK_FALSE(engine.AddRule("*://x/*"));
    CHECK_FALSE(engine.AddRule("|http://example.com"));
    // Options this engine cannot honour must not be applied as if unscoped.
    CHECK_FALSE(engine.AddRule("||example.com^$script"));
    CHECK_FALSE(engine.AddRule("||example.com^$image,domain=a.com"));
    CHECK(engine.rule_count() == 0);
  }

  SECTION("a malformed line never becomes a catch-all") {
    // Each of these, taken literally, would block enormous swathes of the web.
    CHECK_FALSE(engine.AddRule("||^"));
    CHECK_FALSE(engine.AddRule("."));
    CHECK_FALSE(engine.AddRule(".."));
    CHECK_FALSE(engine.AddRule("||.com^"));
    CHECK_FALSE(engine.AddRule("com"));
    CHECK_FALSE(engine.AddRule("a b c"));
    CHECK(engine.rule_count() == 0);
    // And with nothing loaded, nothing is blocked.
    CHECK_FALSE(engine.ShouldBlock("https://example.com/", "site.test"));
  }
}

TEST_CASE("Hosts are matched case- and dot-insensitively",
          "[filter][parsing]") {
  const Engine engine = Built({"||Tracker.Example^"});
  CHECK(engine.ShouldBlock("https://TRACKER.EXAMPLE/p", "site.test"));
  CHECK(engine.ShouldBlock("https://tracker.example./p", "site.test"));
  CHECK(engine.ShouldBlock("https://A.Tracker.Example/p", "site.test"));
}

TEST_CASE("Registrable domain and third-party detection", "[filter][party]") {
  CHECK(RegistrableDomain("a.b.example.com") == "example.com");
  CHECK(RegistrableDomain("example.com") == "example.com");
  CHECK(RegistrableDomain("localhost") == "localhost");

  CHECK(IsThirdParty("ads.other.com", "example.com"));
  CHECK_FALSE(IsThirdParty("cdn.example.com", "www.example.com"));
  // Unknown context must not be treated as third party.
  CHECK_FALSE(IsThirdParty("ads.other.com", ""));
  CHECK_FALSE(IsThirdParty("", "example.com"));

  // Documented limitation: a two-label public suffix reduces to the suffix, so
  // these two look same-site to each other. Asserted so the day someone adds a
  // real public suffix list, this test fails and tells them to update it.
  CHECK_FALSE(IsThirdParty("a.co.uk", "b.co.uk"));
}

TEST_CASE("An empty engine blocks nothing at all", "[filter][safety]") {
  const Engine engine;
  CHECK(engine.rule_count() == 0);
  CHECK_FALSE(engine.ShouldBlock("https://anything.example/x", "site.test"));
  CHECK(engine.Classify("https://anything.example/x", "site.test") ==
        Decision::kAllow);
  // Degenerate inputs must not match either.
  CHECK_FALSE(engine.ShouldBlock("", "site.test"));
  CHECK_FALSE(engine.ShouldBlock("not a url", ""));
}
