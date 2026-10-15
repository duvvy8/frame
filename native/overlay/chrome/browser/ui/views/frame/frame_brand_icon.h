// Original Frame vector mark. GPL-3.0.
#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_FRAME_BRAND_ICON_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_FRAME_BRAND_ICON_H_
#include "ui/gfx/vector_icon_types.h"
namespace frame {
#define DECLARE_VECTOR_COMMAND(x) using gfx::x;
DECLARE_VECTOR_COMMANDS
#undef DECLARE_VECTOR_COMMAND
inline constexpr gfx::PathElement kFrameBrandPath[] = {
#include "chrome/app/vector_icons/frame_brand.icon"
};
inline constexpr gfx::VectorIconRep kFrameBrandReps[] = {{kFrameBrandPath}};
inline constexpr gfx::VectorIcon kFrameBrandIcon(kFrameBrandReps, 1, "frame_brand");
}  // namespace frame
#endif
