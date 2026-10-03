"""Apply Frame colors and a first-pass built-in Chromium new-tab design."""

from pathlib import Path
from shutil import copyfile

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "chromium" / "src"


def change(name: str, before: str, after: str) -> None:
    path = SRC / name
    source = path.read_text(encoding="utf-8")
    if after in source:
        print(f"already applied: {name}")
        return
    if source.count(before) != 1:
        raise RuntimeError(f"expected one source match in {name}: {before[:70]!r}")
    path.write_text(source.replace(before, after), encoding="utf-8", newline="")
    print(f"updated: {name}")


def main() -> None:
    color = "chrome/browser/ui/color/chrome_color_mixer.cc"
    marker = "  // Apply high contrast recipes if necessary.\n"
    change(color, marker,
           "  // Frame's indigo-to-sapphire browser chrome. High-contrast mode\n"
           "  // keeps Chromium's accessibility overrides below.\n"
           "  mixer[ui::kColorFrameActive] = {SkColorSetRGB(31, 15, 52)};\n"
           "  mixer[ui::kColorFrameInactive] = {SkColorSetRGB(26, 18, 42)};\n"
           "  mixer[kColorToolbar] = {SkColorSetRGB(42, 31, 91)};\n"
           "  mixer[kColorToolbarText] = {SK_ColorWHITE};\n"
           "  mixer[kColorToolbarButtonIcon] = {SkColorSetRGB(211, 206, 242)};\n"
           "  mixer[kColorTabBackgroundActiveFrameActive] = {\n"
           "      SkColorSetRGB(61, 49, 149)};\n"
           "  mixer[kColorTabBackgroundInactiveFrameActive] = {\n"
           "      SkColorSetRGB(49, 36, 103)};\n"
           "  mixer[kColorLocationBarBackground] = {SkColorSetRGB(30, 26, 66)};\n\n"
           + marker)

    ntp = "chrome/browser/resources/new_tab_page/"
    build = ntp + "BUILD.gn"
    change(build,
           '    "$target_gen_dir/icons/resources.grdp",\n',
           '    "$target_gen_dir/icons/resources.grdp",\n'
           '    "$target_gen_dir/fonts/resources.grdp",\n')
    change(build,
           '    "icons:build_grdp",\n',
           '    "icons:build_grdp",\n'
           '    "fonts:build_grdp",\n')
    font_dir = SRC / ntp / "fonts"
    font_dir.mkdir(exist_ok=True)
    for font in ("Geist.ttf", "IBMPlexSans.ttf", "IBMPlexMono-Regular.ttf"):
        source = ROOT / "assets" / "fonts" / font
        target = font_dir / font
        if not target.exists() or target.read_bytes() != source.read_bytes():
            copyfile(source, target)
            print(f"copied: {target.relative_to(SRC)}")
    font_build = font_dir / "BUILD.gn"
    font_build_content = '''import("//ui/webui/resources/tools/generate_grd.gni")

generate_grd("build_grdp") {
  grd_prefix = "new_tab_page_frame_font"
  out_grd = "$target_gen_dir/resources.grdp"
  input_files = [
    "Geist.ttf",
    "IBMPlexSans.ttf",
    "IBMPlexMono-Regular.ttf",
  ]
  input_files_base_dir = rebase_path(".", "//")
  resource_path_prefix = "fonts"
}
'''
    if not font_build.exists() or font_build.read_text(encoding="utf-8") != font_build_content:
        font_build.write_text(font_build_content, encoding="utf-8", newline="")
        print(f"updated: {font_build.relative_to(SRC)}")

    change(ntp + "new_tab_page.html",
           '    <title>$i18n{title}</title>\n    <style>\n',
           '    <title>Frame — New tab</title>\n    <style>\n'
           '      @font-face { font-family: FrameGeist; src: url("fonts/Geist.ttf"); }\n'
           '      @font-face { font-family: FramePlexSans; src: url("fonts/IBMPlexSans.ttf"); }\n'
           '      @font-face { font-family: FramePlexMono; src: url("fonts/IBMPlexMono-Regular.ttf"); }\n')
    change(ntp + "new_tab_page.html",
           '        background: $i18n{backgroundColor};\n',
           '        background: radial-gradient(circle at 76% 16%, #354fab 0, #241b56 32%, #110d25 76%);\n')
    change(ntp + "app.html",
           '  </ntp-logo>\n',
           '  </ntp-logo>\n'
           '  <h1 id="frameHeading">Frame</h1>\n'
           '  <p id="frameEdition">A SPACE TO BROWSE</p>\n')
    change(ntp + "app.css",
           '#logo {\n  margin-bottom: var(--ntp-logo-margin-bottom, 38px);\n}\n',
           '#logo {\n  display: none;\n}\n\n'
           '#frameHeading {\n'
           '  color: #f7f4ff;\n'
           '  font: 600 clamp(44px, 6vw, 74px)/1 FramePlexSans, sans-serif;\n'
           '  letter-spacing: -0.045em;\n'
           '  margin: min(18vh, 170px) 0 8px;\n'
           '}\n\n'
           '#frameEdition {\n'
           '  color: #b9b3db;\n'
           '  font: 11px FramePlexMono, monospace;\n'
           '  letter-spacing: 0.2em;\n'
           '  margin: 0 0 34px;\n'
           '}\n\n'
           '#oneGoogleBarStackingContext,\n#oneGoogleBar {\n'
           '  display: none;\n'
           '}\n')
    change(ntp + "app.css",
           ':host {\n  --cr-composebox-input-placeholder-color:',
           ':host {\n  font-family: FrameGeist, sans-serif;\n'
           '  --cr-composebox-input-placeholder-color:')


if __name__ == "__main__":
    main()
