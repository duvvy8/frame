# Frame — milestone 1 development project

Frame is being developed as a fork of the **full Chromium browser** for Windows. This repository contains a pinned Chromium revision, a small development branding overlay, three local Manifest V3 extensions, and Windows build and launch scripts. Chromium's browser UI, page renderer, profile system, and extension platform remain the foundation.

**Current state:** the clean Chromium baseline and the first Frame build both compiled successfully. `Frame.exe` launched, loaded a website, and opened a normal window under its own profile. Automated page, tab, extension, theme, and profile checks passed; native toolbar and omnibox interaction still need a manual check because Windows screen capture timed out. See [PROGRESS.md](PROGRESS.md) for exact results and [BUILD_WINDOWS.md](BUILD_WINDOWS.md) for reproduction.

The [previous Frame prototype](https://github.com/duvvy8/frame) was cloned for read-only visual inspection. Its `src/renderer/shell.css` uses a near-black base with electric blue and violet ambient colour; this first pass keeps that direction more restrained. Ember's [README](https://github.com/v11exe/ember/blob/main/README.md) and [roadmap](https://github.com/v11exe/ember/blob/main/ROADMAP.md) inform [ROADMAP.md](ROADMAP.md). Neither reference repository is modified or included in this project's build.

## What Frame adds at this stage

- `scripts/apply_frame.py` changes a few Chromium product strings and the Windows executable resource's branding, and substitutes an explicitly temporary DEV icon. It leaves Chromium's browser architecture intact.
- `scripts/launch-frame.cmd` runs `Frame.exe` with `%LOCALAPPDATA%\Frame Dev\User Data`, isolated from Chrome, Edge, and default Chromium profiles. Direct `Frame.exe` launch also selects this profile through the small Chromium source overlay.
- `extensions/frame-theme` supplies a restrained dark blue and purple browser theme using Chromium's theme extension mechanism; it appeared as the active theme in Appearance settings.
- `extensions/frame-new-tab` supplies a simple new-tab page using Geist and Geist Mono. These fonts are **not yet applied to native toolbar controls**.
- `extensions/frame-sidebar` is an early Frame side panel with favourite sites, quick searches in its own address field, tab search, browser utility links, and a user-triggered Wayback lookup. It opened in Chromium's native side panel in a headed test. The panel defaults to the left, but it is not an Ember-style permanent rail yet.

No installer, logo, low-memory claim, or enhanced-privacy claim is included.

Double-click `Open Frame.cmd` in this folder to launch the current development build with its separate profile. The output is a development build, not an installer.

## Browser layers, in plain language

| Layer | Frame file | What it does |
| --- | --- | --- |
| Chromium source and build | `CHROMIUM_REVISION`, `scripts/build-*.cmd` | Pins and compiles the complete browser, including its existing navigation and tab machinery. |
| Browser window/UI | `scripts/apply_frame.py`, `extensions/frame-theme`, `extensions/frame-sidebar` | Changes product and window-title strings, colours, and the default side panel side. Back, forward, reload, tabs, and the omnibox remain Chromium's existing UI. Page history and tabs were checked through browser automation; native controls still need manual verification. |
| Profile | `scripts/launch-frame.cmd` | Gives Frame a separate on-disk profile for settings, history, and extensions. |
| Page rendering | `extensions/frame-new-tab` | Supplies only the new-tab page; ordinary websites still use Chromium's Blink renderer and V8 JavaScript engine. |
| Extensions | `extensions/*` | Uses Chromium's existing extension system for the appearance pass. `chrome://extensions` opened, and an unpacked official uBlock Origin Lite release filtered one controlled request in a separate test profile. |

Fonts and licenses are in the extensions' `fonts` directories. Geist and Geist Mono come from the [Google Fonts repository](https://github.com/google/fonts), under the SIL Open Font License.
