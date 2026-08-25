// Tests for the omnibox rule and for JSON escaping.
//
// Both handle input Frame does not control: whatever is typed into the address
// bar, and whatever a page decides to call itself.

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "shared/url_util.h"

using frame::url::JsonEscape;
using frame::url::NormalizeUrl;
using frame::url::UrlEncode;

TEST_CASE("Omnibox sends hostnames to the site", "[url]") {
  CHECK(NormalizeUrl("example.com") == "https://example.com");
  CHECK(NormalizeUrl("en.wikipedia.org") == "https://en.wikipedia.org");
  CHECK(NormalizeUrl("example.com/a/b?c=d") == "https://example.com/a/b?c=d");

  SECTION("surrounding whitespace is not part of the address") {
    CHECK(NormalizeUrl("  example.com  ") == "https://example.com");
  }
}

TEST_CASE("Omnibox sends everything else to search", "[url]") {
  CHECK(NormalizeUrl("hello world") == "https://www.google.com/search?q=hello+world");

  SECTION("a phrase containing a dot is still a search, because of the space") {
    CHECK(NormalizeUrl("what is example.com") ==
          "https://www.google.com/search?q=what+is+example.com");
  }

  SECTION("a bare word is a search, not a hostname") {
    CHECK(NormalizeUrl("wikipedia") == "https://www.google.com/search?q=wikipedia");
  }

  SECTION("a trailing dot has nothing after it, so it is not a host") {
    CHECK(NormalizeUrl("example.") == "https://www.google.com/search?q=example.");
  }

  SECTION("a leading dot has nothing before it either") {
    CHECK(NormalizeUrl(".com") == "https://www.google.com/search?q=.com");
  }
}

TEST_CASE("Omnibox passes explicit schemes through untouched", "[url]") {
  CHECK(NormalizeUrl("https://example.com") == "https://example.com");
  CHECK(NormalizeUrl("http://example.com") == "http://example.com");
  CHECK(NormalizeUrl("about:blank") == "about:blank");
  CHECK(NormalizeUrl("data:text/html,hi") == "data:text/html,hi");

  SECTION("frame:// is ours and must not be searched for") {
    // Regression: without this the new tab page became a Google search for
    // the string "frame://newtab".
    CHECK(NormalizeUrl("frame://newtab") == "frame://newtab");
  }

  SECTION("localhost is a host even without a dot") {
    CHECK(NormalizeUrl("localhost") == "http://localhost");
    CHECK(NormalizeUrl("localhost:8080") == "http://localhost:8080");
  }
}

TEST_CASE("Empty omnibox input goes nowhere", "[url]") {
  CHECK(NormalizeUrl("") == "about:blank");
  CHECK(NormalizeUrl("   ") == "about:blank");
}

TEST_CASE("URL encoding escapes what a query string cannot carry", "[url]") {
  CHECK(UrlEncode("a b") == "a+b");
  CHECK(UrlEncode("a&b=c") == "a%26b%3Dc");
  CHECK(UrlEncode("hello") == "hello");
  CHECK(UrlEncode("a-b_c.d~e") == "a-b_c.d~e");
  CHECK(UrlEncode("100%") == "100%25");
}

// The state payload is executed as script in the chrome surfaces, so a title
// that escapes its string literal is a code-execution bug, not a display one.
TEST_CASE("JSON escaping contains hostile page titles", "[url][security]") {
  SECTION("a quote cannot close the string") {
    CHECK(JsonEscape("say \"hi\"") == "say \\\"hi\\\"");
  }

  SECTION("a backslash cannot escape the closing quote") {
    // Without this, a title ending in a backslash would escape the quote that
    // terminates it and swallow the rest of the payload.
    CHECK(JsonEscape("back\\slash") == "back\\\\slash");
    CHECK(JsonEscape("trailing\\") == "trailing\\\\");
  }

  SECTION("newlines cannot break the statement") {
    CHECK(JsonEscape("a\nb") == "a\\nb");
    CHECK(JsonEscape("a\r\nb") == "a\\r\\nb");
    CHECK(JsonEscape("a\tb") == "a\\tb");
  }

  SECTION("control characters become escapes, not raw bytes") {
    CHECK(JsonEscape(std::string("a\x01""b")) == "a\\u0001b");
    CHECK(JsonEscape(std::string("\x1f")) == "\\u001f");
  }

  SECTION("script-terminating characters are escaped too") {
    // The payload is embedded in a script context, where a literal </script>
    // ends the block regardless of being inside a JSON string.
    CHECK(JsonEscape("</script>") == "\\u003c/script\\u003e");
    CHECK(JsonEscape("a&b") == "a\\u0026b");
  }

  SECTION("an attempted breakout stays inside the string") {
    // The property that matters is that no quote is left UNESCAPED. Searching
    // for the substring "\"; is not that property — it appears inside the
    // perfectly safe \"; and would fail on correct output.
    auto has_unescaped_quote = [](const std::string& text) {
      for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '"') {
          continue;
        }
        size_t slashes = 0;
        for (size_t j = i; j-- > 0 && text[j] == '\\';) {
          ++slashes;
        }
        if (slashes % 2 == 0) {
          return true;  // Not escaped: the string literal ends here.
        }
      }
      return false;
    };

    CHECK(has_unescaped_quote("\";window.close();//"));
    CHECK_FALSE(has_unescaped_quote(JsonEscape("\";window.close();//")));
    CHECK_FALSE(has_unescaped_quote(JsonEscape("ends with a backslash\\")));
    CHECK_FALSE(has_unescaped_quote(JsonEscape("\\\" mixed \\\\\" case")));
  }

  SECTION("ordinary titles are left alone") {
    CHECK(JsonEscape("Example Domain") == "Example Domain");
    CHECK(JsonEscape("") == "");
  }
}
