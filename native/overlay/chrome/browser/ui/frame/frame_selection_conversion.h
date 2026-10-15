// Frame local selection conversions. GPL-3.0; see Frame repository LICENSE.
#ifndef CHROME_BROWSER_UI_FRAME_FRAME_SELECTION_CONVERSION_H_
#define CHROME_BROWSER_UI_FRAME_FRAME_SELECTION_CONVERSION_H_
#include <optional>
#include <string>
namespace frame {
std::optional<std::u16string> ConvertMeasurement(const std::u16string& selection,
                                                bool metric);
}  // namespace frame
#endif
