// Frame native shell. GPL-3.0; see the Frame repository LICENSE.
#ifndef CHROME_BROWSER_THEMES_FRAME_THEME_SUPPLIER_H_
#define CHROME_BROWSER_THEMES_FRAME_THEME_SUPPLIER_H_

#include "base/memory/scoped_refptr.h"
#include "ui/color/color_provider_key.h"

namespace frame {
inline constexpr char kBlueShellPref[] = "frame.appearance.blue_shell";
inline constexpr char kAlwaysShowTabsPref[] = "frame.appearance.always_show_tabs";
inline constexpr char kRailVisiblePref[] = "frame.appearance.show_rail";
inline constexpr char kVisualMruPref[] = "frame.tabs.visual_mru";
inline constexpr char kSelectionConversionsPref[] = "frame.selection.conversions";
inline constexpr char kMetricConversionsPref[] = "frame.selection.metric";
inline constexpr char kSelectionPopupPref[] = "frame.selection.popup";

// Immutable suppliers are part of the ColorProviderKey, so cached palettes are
// distinct from the ordinary theme. Switching off Frame restores that theme.
scoped_refptr<ui::ColorProviderKey::ThemeInitializerSupplier> GetThemeSupplier(
    scoped_refptr<ui::ColorProviderKey::ThemeInitializerSupplier> original);
}  // namespace frame

#endif
