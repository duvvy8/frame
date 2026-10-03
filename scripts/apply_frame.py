"""Apply Frame's narrow branding and startup overlay to pinned Chromium.

Re-running is safe; unexpected upstream text fails loudly instead of silently
producing a partly branded build.
"""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "chromium" / "src"


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    if new in text and old not in text:
        print(f"already applied: {path.relative_to(SRC)}")
        return
    if text.count(old) != 1:
        raise RuntimeError(f"expected one match in {path}: {old[:70]!r}")
    path.write_text(text.replace(old, new), encoding="utf-8", newline="")
    print(f"updated: {path.relative_to(SRC)}")


def replace_message(path: Path, name: str, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    pattern = rf'(<message name="{re.escape(name)}"[^>]*>\s*){re.escape(old)}(\s*</message>)'
    matches = list(re.finditer(pattern, text))
    if len(matches) != 1:
        if len(re.findall(pattern.replace(re.escape(old), re.escape(new)), text)) == 1:
            print(f"already applied: {name}")
            return
        raise RuntimeError(f"expected one {name}={old!r} in {path}")
    text = re.sub(pattern, lambda m: m.group(1) + new + m.group(2), text, count=1)
    path.write_text(text, encoding="utf-8", newline="")
    print(f"updated: {name}")


def insert_before_once(path: Path, marker: str, insertion: str, sentinel: str) -> None:
    text = path.read_text(encoding="utf-8")
    if sentinel in text:
        print(f"already applied: {path.relative_to(SRC)} ({sentinel})")
        return
    if text.count(marker) != 1:
        raise RuntimeError(f"expected one marker in {path}: {marker[:70]!r}")
    path.write_text(text.replace(marker, insertion + marker), encoding="utf-8", newline="")
    print(f"updated: {path.relative_to(SRC)} ({sentinel})")


def main() -> None:
    if not (SRC / "chrome" / "app" / "chromium_strings.grd").is_file():
        raise RuntimeError("Chromium source missing; run the official fetch first")
    branding = SRC / "chrome" / "app" / "theme" / "chromium" / "BRANDING"
    replace_once(branding, "PRODUCT_FULLNAME=Chromium\n", "PRODUCT_FULLNAME=Frame\n")
    replace_once(branding, "PRODUCT_SHORTNAME=Chromium\n", "PRODUCT_SHORTNAME=Frame\n")

    strings = SRC / "chrome" / "app" / "chromium_strings.grd"
    # This occurs twice: the Chrome-for-Testing branch and the ordinary branch.
    # Restrict to the non-CfT block so the structure of Chromium's GRIT file stays intact.
    text = strings.read_text(encoding="utf-8")
    old = ('<else>\n          <message name="IDS_PRODUCT_NAME" desc="The Chrome application name" '
           'translateable="false">\n            Chromium\n          </message>\n'
           '          <message name="IDS_SHORT_PRODUCT_NAME" desc="The Chrome application short name." '
           'translateable="false">\n            Chromium\n          </message>')
    new = old.replace("            Chromium\n", "            Frame\n")
    replace_once(strings, old, new)
    replace_message(strings, "IDS_APP_MENU_YOUR_CHROME_HEADER", "Your Chromium", "Your Frame")
    replace_message(strings, "IDS_PRODUCT_DESCRIPTION",
                    "Chromium is a web browser that runs webpages and applications with lightning speed. It's fast, stable, and easy to use. Browse the web more safely with malware and phishing protection built into Chromium.",
                    "Frame is a development browser built from Chromium.")
    replace_message(strings, "IDS_WELCOME_TO_CHROME", "Welcome to Chromium; new browser window opened",
                    "Welcome to Frame; new browser window opened")
    replace_once(
        strings,
        '<ph name="PAGE_TITLE">$1<ex>Google</ex></ph> - Chromium\n            </message>',
        '<ph name="PAGE_TITLE">$1<ex>Google</ex></ph> - Frame\n            </message>',
    )
    replace_once(
        strings,
        '<ph name="PAGE_TITLE">$1<ex>Google</ex></ph> - Network Sign-in - Chromium\n        </message>',
        '<ph name="PAGE_TITLE">$1<ex>Google</ex></ph> - Network Sign-in - Frame\n        </message>',
    )

    settings = SRC / "chrome" / "app" / "settings_chromium_strings.grdp"
    replace_message(settings, "IDS_RELAUNCH_CONFIRMATION_DIALOG_TITLE", "Relaunch Chromium?", "Relaunch Frame?")
    replace_message(settings, "IDS_SETTINGS_ABOUT_PROGRAM", "About Chromium", "About Frame")

    side_panel_prefs = SRC / "chrome" / "browser" / "ui" / "side_panel" / "side_panel_prefs.cc"
    replace_once(
        side_panel_prefs,
        "  // When in RTL mode, the side panel should default to the left of the screen.\n"
        "  // Otherwise, the side panel should default to the right side of the screen.\n"
        "  // TODO(dljames): Add enum values kAlternateSide / kDefaultSide that will\n"
        "  // replace false and true respectively.\n"
        "  registry->RegisterBooleanPref(prefs::kSidePanelHorizontalAlignment,\n"
        "                                !base::i18n::IsRTL());\n",
        "  // Frame places side panels on the leading edge by default. The setting\n"
        "  // remains user-configurable through Chromium's existing preferences.\n"
        "  registry->RegisterBooleanPref(prefs::kSidePanelHorizontalAlignment,\n"
        "                                base::i18n::IsRTL());\n",
    )

    paths_win = SRC / "chrome" / "common" / "chrome_paths_win.cc"
    replace_once(
        paths_win,
        "bool GetDefaultUserDataDirectory(base::FilePath* result) {\n"
        "  if (!base::PathService::Get(base::DIR_LOCAL_APP_DATA, result))\n"
        "    return false;\n"
        "  *result = result->Append(install_static::GetChromeInstallSubDirectory());\n"
        "  *result = result->Append(chrome::kUserDataDirname);\n"
        "  return true;\n}\n",
        "bool GetDefaultUserDataDirectory(base::FilePath* result) {\n"
        "  if (!base::PathService::Get(base::DIR_LOCAL_APP_DATA, result))\n"
        "    return false;\n"
        "  *result = result->Append(FILE_PATH_LITERAL(\"Frame Dev\"));\n"
        "  *result = result->Append(chrome::kUserDataDirname);\n"
        "  return true;\n}\n",
    )
    replace_once(
        paths_win,
        "bool GetDefaultRoamingUserDataDirectory(base::FilePath* result) {\n"
        "  if (!base::PathService::Get(base::DIR_ROAMING_APP_DATA, result))\n"
        "    return false;\n"
        "  *result = result->Append(install_static::GetChromeInstallSubDirectory());\n"
        "  *result = result->Append(chrome::kUserDataDirname);\n"
        "  return true;\n}\n",
        "bool GetDefaultRoamingUserDataDirectory(base::FilePath* result) {\n"
        "  if (!base::PathService::Get(base::DIR_ROAMING_APP_DATA, result))\n"
        "    return false;\n"
        "  *result = result->Append(FILE_PATH_LITERAL(\"Frame Dev\"));\n"
        "  *result = result->Append(chrome::kUserDataDirname);\n"
        "  return true;\n}\n",
    )

    # On Windows chrome_elf resolves the profile before chrome_paths_win.cc is
    # consulted. Keep both defaults aligned so direct Frame.exe launches do not
    # reuse Chromium's normal User Data directory.
    early_profile = SRC / "chrome" / "install_static" / "user_data_dir.cc"
    replace_once(
        early_profile,
        "  AppendChromeInstallSubDirectory(mode, true /* include_suffix */, result);\n"
        "  result->push_back(L'\\\\');\n"
        "  result->append(L\"User Data\");\n",
        "  result->append(L\"Frame Dev\");\n"
        "  result->push_back(L'\\\\');\n"
        "  result->append(L\"User Data\");\n",
    )

    install_modes = SRC / "chrome" / "install_static" / "chromium_install_modes.h"
    replace_once(install_modes, 'kProductPathName[] = L"Chromium";',
                 'kProductPathName[] = L"Frame";')
    replace_once(install_modes, '.base_app_name = L"Chromium",',
                 '.base_app_name = L"Frame",')
    replace_once(install_modes, '.base_app_id = L"Chromium",',
                 '.base_app_id = L"Frame",')
    replace_once(install_modes,
                 '.supports_set_as_default_browser =\n            true,',
                 '.supports_set_as_default_browser =\n            false,')

    main_delegate = SRC / "chrome" / "app" / "chrome_main_delegate.cc"
    # The window shell is implemented in Chromium Views. No bundled extension
    # is injected into browser startup; ordinary user extensions still work.
    source = main_delegate.read_text(encoding="utf-8")
    start = "  // Frame's bundled appearance assets also load when Frame.exe is opened\n"
    end = "  if (is_browser &&\n      !command_line.HasSwitch(switches::kNoDefaultBrowserCheck)) {\n"
    if start in source:
        before, rest = source.split(start, 1)
        _, after = rest.split(end, 1)
        main_delegate.write_text(before + end + after, encoding="utf-8", newline="")
        print(f"removed extension startup: {main_delegate.relative_to(SRC)}")
    insert_before_once(
        main_delegate,
        "  // Browser should not be sandboxed.\n",
        "  if (is_browser &&\n"
        "      !command_line.HasSwitch(switches::kNoDefaultBrowserCheck)) {\n"
        "    base::CommandLine::ForCurrentProcess()->AppendSwitch(\n"
        "        switches::kNoDefaultBrowserCheck);\n"
        "  }\n\n",
        "kNoDefaultBrowserCheck",
    )

    icon = ROOT / "assets" / "frame-dev.ico"
    if not icon.is_file():
        raise RuntimeError(f"missing temporary icon: {icon}")
    dest = SRC / "chrome" / "app" / "theme" / "chromium" / "win" / "chromium.ico"
    if dest.read_bytes() != icon.read_bytes():
        dest.write_bytes(icon.read_bytes())
        print(f"updated: {dest.relative_to(SRC)} (temporary DEV icon)")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"Frame overlay failed: {exc}", file=sys.stderr)
        sys.exit(1)
