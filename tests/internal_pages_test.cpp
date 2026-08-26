// Tests for the frame:// allowlist.
//
// This is a security boundary: it decides which files a page can cause Frame to
// read off disk. It is verified here rather than by hand, so a later edit
// cannot quietly widen it.

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "shared/internal_pages.h"

using frame::internal_pages::Find;
using frame::internal_pages::Lookup;
using frame::internal_pages::ResolveName;

TEST_CASE("A bare host resolves to its page", "[scheme]") {
  CHECK(ResolveName("newtab", "") == "newtab.html");
  CHECK(ResolveName("newtab", "/") == "newtab.html");
  CHECK(ResolveName("unreachable", "") == "unreachable.html");
}

TEST_CASE("A path resolves to the named asset", "[scheme]") {
  CHECK(ResolveName("newtab", "/shell.css") == "shell.css");
  CHECK(ResolveName("newtab", "shell.css") == "shell.css");
  CHECK(ResolveName("newtab", "//logo.svg") == "logo.svg");
}

TEST_CASE("Allowlisted resources are served with the right type", "[scheme]") {
  const auto* page = Lookup("newtab", "");
  REQUIRE(page != nullptr);
  CHECK(std::string(page->name) == "newtab.html");
  CHECK(std::string(page->mime) == "text/html");

  const auto* style = Lookup("newtab", "/shell.css");
  REQUIRE(style != nullptr);
  CHECK(std::string(style->mime) == "text/css");

  const auto* script = Lookup("newtab", "/shell.js");
  REQUIRE(script != nullptr);
  CHECK(std::string(script->mime) == "text/javascript");

  const auto* image = Lookup("newtab", "/logo.svg");
  REQUIRE(image != nullptr);
  CHECK(std::string(image->mime) == "image/svg+xml");
}

TEST_CASE("Traversal cannot be expressed", "[scheme][security]") {
  SECTION("dot-dot in any form") {
    CHECK(Lookup("newtab", "/../shell.css") == nullptr);
    CHECK(Lookup("newtab", "/../../frame.exe") == nullptr);
    CHECK(Find("..") == nullptr);
    CHECK(Find("../newtab.html") == nullptr);
  }

  SECTION("separators, forward or back") {
    CHECK(Find("sub/newtab.html") == nullptr);
    CHECK(Find("sub\\newtab.html") == nullptr);
    CHECK(Lookup("newtab", "/a/b/shell.css") == nullptr);
  }

  SECTION("absolute and drive-qualified paths") {
    CHECK(Find("C:/Windows/System32/drivers/etc/hosts") == nullptr);
    CHECK(Find("C:\\Windows\\win.ini") == nullptr);
    CHECK(Lookup("newtab", "/C:/Windows/win.ini") == nullptr);
  }

  SECTION("UNC paths") {
    CHECK(Find("\\\\server\\share\\file") == nullptr);
  }
}

TEST_CASE("Anything not named is refused", "[scheme][security]") {
  SECTION("an unknown host has no page") {
    // settings, history and bookmarks have each stood here in turn, as hosts
    // that did not exist yet. They all do now, so they prove nothing about
    // refusal — these do.
    CHECK(Lookup("extensions", "") == nullptr);
    CHECK(Lookup("flags", "") == nullptr);
    CHECK(Lookup("permissions", "") == nullptr);
    CHECK(Lookup("", "") == nullptr);
  }

  SECTION("the hosts that were added since do resolve") {
    // The other half of the same claim: the allowlist refuses what is not on
    // it, and serves what is. A test that only ever checked the first half
    // would still pass if the table were empty.
    CHECK(Lookup("settings", "") != nullptr);
    CHECK(Lookup("history", "") != nullptr);
    CHECK(Lookup("downloads", "") != nullptr);
    CHECK(Lookup("bookmarks", "") != nullptr);
    CHECK(Lookup("newtab", "") != nullptr);
    // The context menu is served over frame:// like everything else, which is
    // what gives it a real origin and access to the bridge.
    CHECK(Lookup("menu", "") != nullptr);
  }

  SECTION("a real file that is simply not on the list") {
    CHECK(Find("libcef.dll") == nullptr);
    CHECK(Find("frame.exe") == nullptr);
    CHECK(Find("frame-console.log") == nullptr);
  }

  SECTION("case must match exactly, so no near-miss slips through") {
    CHECK(Find("NewTab.html") == nullptr);
    CHECK(Find("SHELL.CSS") == nullptr);
  }

  SECTION("empty and whitespace names") {
    CHECK(Find("") == nullptr);
    CHECK(Find(" ") == nullptr);
  }
}

TEST_CASE("Every allowlisted resource is well formed", "[scheme]") {
  // Guards the table itself: an entry with a separator in it would be
  // unreachable at best and a traversal at worst.
  for (const auto& resource : frame::internal_pages::kResources) {
    const std::string name(resource.name);
    INFO("resource: " << name);
    CHECK_FALSE(name.empty());
    CHECK(name.find('/') == std::string::npos);
    CHECK(name.find('\\') == std::string::npos);
    CHECK(name.find("..") == std::string::npos);
    CHECK(std::string(resource.mime).find('/') != std::string::npos);
    // Each entry must be reachable through the lookup it exists for.
    CHECK(Find(name) != nullptr);
  }
}
