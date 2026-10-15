// Frame local selection conversions. GPL-3.0; see Frame repository LICENSE.
#include "chrome/browser/ui/frame/frame_selection_conversion.h"
#include <cmath>
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
namespace frame {
// Deliberately recognise a complete measurement, never prose or ambiguous
// currencies/time zones. No renderer script, network request or rate cache.
std::optional<std::u16string> ConvertMeasurement(
    const std::u16string& selection, bool metric) {
  if (selection.empty() || selection.size() > 128) {
    return std::nullopt;
  }
  std::u16string trimmed;
  base::TrimWhitespace(selection, base::TRIM_ALL, &trimmed);
  std::string text = base::ToLowerASCII(base::UTF16ToUTF8(trimmed));
  size_t unit_start = text.find_first_not_of("+-0123456789.");
  if (unit_start == std::string::npos || unit_start == 0) {
    return std::nullopt;
  }
  double value;
  if (!base::StringToDouble(text.substr(0, unit_start), &value) ||
      !std::isfinite(value)) {
    return std::nullopt;
  }
  std::string unit(base::TrimWhitespaceASCII(text.substr(unit_start),
                                           base::TRIM_ALL));
  enum class Dimension { kLength, kMass, kVolume };
  struct Unit {
    const char* name;
    Dimension dimension;
    double factor;
  };
  static constexpr Unit units[] = {
      {"mm", Dimension::kLength, .001}, {"cm", Dimension::kLength, .01},
      {"m", Dimension::kLength, 1}, {"metres", Dimension::kLength, 1},
      {"meters", Dimension::kLength, 1}, {"km", Dimension::kLength, 1000},
      {"in", Dimension::kLength, .0254}, {"inches", Dimension::kLength, .0254},
      {"ft", Dimension::kLength, .3048}, {"feet", Dimension::kLength, .3048},
      {"yd", Dimension::kLength, .9144}, {"mi", Dimension::kLength, 1609.344},
      {"miles", Dimension::kLength, 1609.344},
      {"mg", Dimension::kMass, .000001}, {"g", Dimension::kMass, .001},
      {"kg", Dimension::kMass, 1}, {"oz", Dimension::kMass, .028349523125},
      {"lb", Dimension::kMass, .45359237}, {"lbs", Dimension::kMass, .45359237},
      {"ml", Dimension::kVolume, .001}, {"l", Dimension::kVolume, 1},
      {"litres", Dimension::kVolume, 1}, {"liters", Dimension::kVolume, 1},
      {"us fl oz", Dimension::kVolume, .0295735295625},
      {"us gal", Dimension::kVolume, 3.785411784},
      {"us gallon", Dimension::kVolume, 3.785411784},
      {"uk fl oz", Dimension::kVolume, .0284130625},
      {"uk gal", Dimension::kVolume, 4.54609},
      {"uk gallon", Dimension::kVolume, 4.54609}};
  double result = 0;
  std::string target;
  if (unit == "°c" || unit == "celsius" || unit == "°f" ||
      unit == "fahrenheit") {
    const bool celsius = unit == "°c" || unit == "celsius";
    double c = celsius ? value : (value - 32) * 5 / 9;
    if (c < -273.15) {
      return std::nullopt;
    }
    result = metric ? c : c * 9 / 5 + 32;
    target = metric ? "°C" : "°F";
  } else {
    const Unit* source = nullptr;
    for (const auto& candidate : units) {
      if (unit == candidate.name) {
        source = &candidate;
        break;
      }
    }
    if (!source) {
      return std::nullopt;
    }
    const double canonical = value * source->factor;
    double factor = 1;
    switch (source->dimension) {
      case Dimension::kLength:
        if (metric) {
          target = std::abs(canonical) >= 1000 ? "km" : "m";
          factor = target == "km" ? 1000 : 1;
        } else {
          target = std::abs(canonical) >= 1609.344 ? "mi" : "ft";
          factor = target == "mi" ? 1609.344 : .3048;
        }
        break;
      case Dimension::kMass:
        if (metric) {
          target = std::abs(canonical) >= 1 ? "kg" : "g";
          factor = target == "kg" ? 1 : .001;
        } else {
          target = std::abs(canonical) >= .45359237 ? "lb" : "oz";
          factor = target == "lb" ? .45359237 : .028349523125;
        }
        break;
      case Dimension::kVolume:
        target = metric ? (std::abs(canonical) >= 1 ? "L" : "mL") : "US fl oz";
        factor = metric ? (target == "L" ? 1 : .001) : .0295735295625;
        break;
    }
    result = canonical / factor;
  }
  if (!std::isfinite(result) ||
      base::ToLowerASCII(target) == unit) {
    return std::nullopt;
  }
  // Six significant digits; normalise negative zero without losing tiny values.
  return base::UTF8ToUTF16(base::StringPrintf("%.6g", result == 0 ? 0 : result) +
                           " " + target);
}

}  // namespace frame
