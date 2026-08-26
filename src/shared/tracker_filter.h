// Frame — the tracker/ad filter engine.
//
// Framework-free and header-only, like everything else in shared/, so the
// matching rules can be tested directly rather than only through a running
// browser. This is the component that decides whether a request is allowed to
// leave the machine, so "testable in isolation" is not a nicety.
//
// WHY HOST MATCHING RATHER THAN A FULL ADBLOCK ENGINE
//
// Real filter lists are mostly `||host^` rules, and those are what actually
// stop trackers. A complete Adblock Plus implementation additionally supports
// arbitrary substring patterns, wildcards, regular expressions, per-type
// options and element hiding — and evaluating those means running a pattern
// matcher over every subresource URL, on the IO thread, on every page load.
// That is the design that makes blockers expensive.
//
// So this engine deliberately implements the part that carries almost all of
// the blocking value at a cost that is a hash lookup per label: a request to
// a.b.tracker.example is checked as "a.b.tracker.example", then
// "b.tracker.example", then "tracker.example". Three lookups, no allocation
// beyond the substrings, no regex, no backtracking.
//
// Cosmetic rules (##) are parsed and DISCARDED rather than silently ignored,
// because hiding an element after it has already been downloaded is not
// privacy — the request was the thing that mattered, and it already happened.
//
// THREAD SAFETY: build it once, then treat it as immutable. ShouldBlock() is
// const and takes no locks because CEF calls it on the IO thread while nothing
// is writing. Loading a list after that point would be a data race.

#ifndef FRAME_SHARED_TRACKER_FILTER_H_
#define FRAME_SHARED_TRACKER_FILTER_H_

#include <cstddef>
#include <string>
#include <unordered_set>

#include "shared/url_util.h"

namespace frame::filter {

/// Why a request was allowed or refused. Reported to the UI, and the reason
/// matters: "no rule matched" and "a rule matched but an exception overrode
/// it" look identical from the outside and mean very different things when
/// someone is trying to work out why a site is broken.
enum class Decision {
  kAllow,          // Nothing matched.
  kBlock,          // A blocking rule matched.
  kAllowException, // A rule matched, and an @@ exception won.
  kAllowFirstParty // A third-party-only rule matched, but this was first party.
};

/// The registrable part of a host, used to decide first vs third party.
///
/// This is the last two labels — "a.b.example.com" -> "example.com" — which is
/// right for the common case and wrong for public suffixes with two labels,
/// where "bbc.co.uk" reduces to "co.uk". The consequence is conservative in the
/// safe direction: two hosts under the same public suffix look FIRST party to
/// each other, so a third-party-only rule declines to fire. It under-blocks
/// rather than breaking sites, which is the correct way to be wrong here.
///
/// Doing better requires the Public Suffix List, which is a 15,000-entry table
/// that has to be shipped and kept current; worth it later, not worth
/// pretending about now.
inline std::string RegistrableDomain(const std::string& host) {
  const size_t last_dot = host.find_last_of('.');
  if (last_dot == std::string::npos || last_dot == 0) {
    return host;
  }
  const size_t prev_dot = host.find_last_of('.', last_dot - 1);
  if (prev_dot == std::string::npos) {
    return host;
  }
  return host.substr(prev_dot + 1);
}

/// True when the request host belongs to a different site than the document.
inline bool IsThirdParty(const std::string& request_host,
                         const std::string& document_host) {
  if (request_host.empty() || document_host.empty()) {
    return false;  // Unknown context is not evidence of third-partyness.
  }
  return RegistrableDomain(request_host) != RegistrableDomain(document_host);
}

class Engine {
 public:
  Engine() = default;

  /// Feeds one line of a filter list. Returns true if it became a rule.
  ///
  /// Accepts the shapes that actually appear in the wild:
  ///   ||tracker.example^              block that host and its subdomains
  ///   ||tracker.example^$third-party  ... but only cross-site
  ///   @@||cdn.example^                exception, wins over any block
  ///   0.0.0.0 tracker.example         hosts-file syntax
  ///   tracker.example                 a bare domain
  ///   ! comment / [Adblock Plus 2.0]  ignored
  ///   example.com##.ad-banner         cosmetic, deliberately discarded
  bool AddRule(const std::string& raw) {
    const std::string line = frame::url::Trim(raw);
    if (line.empty() || line[0] == '!' || line[0] == '#' || line[0] == '[') {
      return false;
    }
    // Cosmetic rules: dropped on purpose. See the header comment.
    if (line.find("##") != std::string::npos ||
        line.find("#@#") != std::string::npos) {
      return false;
    }

    std::string rest = line;
    bool exception = false;
    if (rest.rfind("@@", 0) == 0) {
      exception = true;
      rest = rest.substr(2);
    }

    if (rest.rfind("||", 0) == 0) {
      rest = rest.substr(2);
    } else if (rest.rfind("0.0.0.0 ", 0) == 0) {
      rest = rest.substr(8);
    } else if (rest.rfind("127.0.0.1 ", 0) == 0) {
      rest = rest.substr(10);
    } else if (rest.find('/') != std::string::npos ||
               rest.find('*') != std::string::npos) {
      // A path or wildcard pattern. Not something host matching can answer, and
      // guessing would block the wrong things.
      return false;
    }

    bool third_party_only = false;
    const size_t options = rest.find('$');
    if (options != std::string::npos) {
      const std::string opts = rest.substr(options + 1);
      rest = rest.substr(0, options);
      // Only the option that changes HOST matching is honoured. A rule scoped
      // to a resource type this engine cannot see is dropped rather than
      // applied too broadly.
      if (opts.find("third-party") != std::string::npos ||
          opts.find("3p") != std::string::npos) {
        third_party_only = true;
      } else {
        return false;
      }
    }

    // Trailing separator from ||host^ form.
    while (!rest.empty() && (rest.back() == '^' || rest.back() == '|' ||
                            rest.back() == '/')) {
      rest.pop_back();
    }
    if (rest.rfind("|", 0) == 0) {
      return false;  // |http://... anchored to a full URL.
    }

    const std::string host = Normalize(rest);
    if (!PlausibleHost(host)) {
      return false;
    }

    if (exception) {
      exceptions_.insert(host);
    } else if (third_party_only) {
      third_party_.insert(host);
    } else {
      blocked_.insert(host);
    }
    return true;
  }

  /// The whole decision for one request.
  Decision Classify(const std::string& request_url,
                    const std::string& document_host) const {
    const std::string host = Normalize(HostOfUrl(request_url));
    if (host.empty()) {
      return Decision::kAllow;
    }
    const std::string doc = Normalize(document_host);
    const bool third_party = IsThirdParty(host, doc);

    // Walk the host up one label at a time: a.b.c -> b.c -> c.
    for (size_t at = 0; at != std::string::npos;) {
      const std::string candidate = host.substr(at);
      if (exceptions_.count(candidate)) {
        return Decision::kAllowException;
      }
      if (blocked_.count(candidate)) {
        return Decision::kBlock;
      }
      if (third_party_.count(candidate)) {
        return third_party ? Decision::kBlock : Decision::kAllowFirstParty;
      }
      const size_t dot = host.find('.', at);
      at = (dot == std::string::npos) ? std::string::npos : dot + 1;
    }
    return Decision::kAllow;
  }

  bool ShouldBlock(const std::string& request_url,
                   const std::string& document_host) const {
    return Classify(request_url, document_host) == Decision::kBlock;
  }

  std::size_t rule_count() const {
    return blocked_.size() + third_party_.size() + exceptions_.size();
  }
  std::size_t exception_count() const { return exceptions_.size(); }

  /// Host component of a URL, without scheme, port, path or credentials.
  static std::string HostOfUrl(const std::string& url) {
    size_t start = url.find("://");
    start = (start == std::string::npos) ? 0 : start + 3;
    const size_t at = url.find('@', start);
    const size_t slash = url.find('/', start);
    if (at != std::string::npos && (slash == std::string::npos || at < slash)) {
      start = at + 1;
    }
    size_t end = url.size();
    for (size_t i = start; i < url.size(); ++i) {
      const char c = url[i];
      if (c == '/' || c == ':' || c == '?' || c == '#') {
        end = i;
        break;
      }
    }
    return url.substr(start, end - start);
  }

 private:
  static std::string Normalize(const std::string& host) {
    std::string out;
    out.reserve(host.size());
    for (char c : host) {
      out.push_back(static_cast<char>(
          (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c));
    }
    // A trailing root dot is legal in DNS and would defeat every lookup.
    while (!out.empty() && out.back() == '.') {
      out.pop_back();
    }
    return out;
  }

  // Guards against a malformed line becoming a rule that blocks everything.
  static bool PlausibleHost(const std::string& host) {
    if (host.size() < 3 || host.find('.') == std::string::npos) {
      return false;
    }
    if (host.front() == '.' || host.find("..") != std::string::npos) {
      return false;
    }
    for (char c : host) {
      const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                      c == '.' || c == '-' || c == '_';
      if (!ok) {
        return false;
      }
    }
    return true;
  }

  std::unordered_set<std::string> blocked_;
  std::unordered_set<std::string> third_party_;
  std::unordered_set<std::string> exceptions_;
};

}  // namespace frame::filter

#endif  // FRAME_SHARED_TRACKER_FILTER_H_
