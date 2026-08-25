// Frame — pure URL and text helpers.
//
// Framework-free on purpose: these are the two places where untrusted input
// reaches decisions that matter, so they live where they can be tested
// directly rather than only through a running browser.

#ifndef FRAME_SHARED_URL_UTIL_H_
#define FRAME_SHARED_URL_UTIL_H_

#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>

namespace frame::url {

inline constexpr char kBlankPage[] = "about:blank";
inline constexpr char kNewTabPage[] = "frame://newtab";
inline constexpr char kSearchPrefix[] = "https://www.google.com/search?q=";

inline std::string Trim(const std::string& value) {
  const size_t first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return std::string();
  }
  const size_t last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

inline bool StartsWith(const std::string& value, const char* prefix) {
  return value.rfind(prefix, 0) == 0;
}

inline std::string UrlEncode(const std::string& value) {
  static const char* kHex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char ch : value) {
    if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      out.push_back(static_cast<char>(ch));
    } else if (ch == ' ') {
      out.push_back('+');
    } else {
      out.push_back('%');
      out.push_back(kHex[ch >> 4]);
      out.push_back(kHex[ch & 0x0F]);
    }
  }
  return out;
}

// True for input that already names a scheme we navigate to as-is.
inline bool HasKnownScheme(const std::string& input) {
  return StartsWith(input, "http://") || StartsWith(input, "https://") ||
         StartsWith(input, "file://") || StartsWith(input, "about:") ||
         StartsWith(input, "data:") || StartsWith(input, "chrome:") ||
         StartsWith(input, "frame:");
}

/// Decides whether omnibox input is a place to go or something to search for.
///
/// Deliberately simple; the bangs and search-shortcut system replaces this
/// wholesale later. The rule is: an explicit scheme wins, then localhost, then
/// "a dot and no whitespace" reads as a hostname. Everything else is a query.
inline std::string NormalizeUrl(const std::string& raw) {
  const std::string input = Trim(raw);
  if (input.empty()) {
    return kBlankPage;
  }
  if (HasKnownScheme(input)) {
    return input;
  }
  if (input == "localhost" || StartsWith(input, "localhost:")) {
    return "http://" + input;
  }

  const bool has_space = input.find(' ') != std::string::npos;
  const size_t dot = input.find('.');
  if (!has_space && dot != std::string::npos && dot > 0 &&
      dot + 1 < input.size()) {
    return "https://" + input;
  }
  return kSearchPrefix + UrlEncode(input);
}

/// Escapes a string for embedding in JSON.
///
/// Page titles are attacker-influenced, and the state payload they end up in
/// is executed as script. An unescaped quote or backslash there would break out
/// of the string literal and into the surrounding code, so this has to be
/// correct rather than convenient.
inline std::string JsonEscape(const std::string& value) {
  std::ostringstream out;
  for (unsigned char ch : value) {
    switch (ch) {
      case '"':
        out << "\\\"";
        break;
      case '\\':
        out << "\\\\";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      // Not required by JSON, but these terminate a script early when the
      // payload is embedded in a <script> context, so they are escaped too.
      case '<':
        out << "\\u003c";
        break;
      case '>':
        out << "\\u003e";
        break;
      case '&':
        out << "\\u0026";
        break;
      default:
        if (ch < 0x20) {
          out << "\\u" << std::hex << std::setfill('0') << std::setw(4)
              << static_cast<int>(ch) << std::dec;
        } else {
          out << static_cast<char>(ch);
        }
    }
  }
  return out.str();
}

}  // namespace frame::url

#endif  // FRAME_SHARED_URL_UTIL_H_
