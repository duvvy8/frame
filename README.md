# Frame — native Chromium development browser

Frame is being developed as a fork of the **full Chromium browser** for Windows. This repository contains a pinned Chromium revision, a small development branding overlay, three local Manifest V3 extensions, and Windows build and launch scripts. Chromium's browser UI, page renderer, profile system, and extension platform remain the foundation.

**Current state (15 October):** the preserved native overlay now implements the selected floating tab dock, compact navigation, caption-side split control, bottom-left browser menu and favicon-only new-tab shortcuts. The dock uses Chromium's real tab model and retains native tab menus, sleeping and session systems. Cloud overlay/geometry checks pass; this redesign has not yet been compiled or visually accepted in the Windows browser. See [floating-dock verification and Windows steps](docs/design/floating-dock-verification.md) for exact build, upgrade and acceptance instructions. Earlier native results remain historical in [PROGRESS.md](PROGRESS.md); [ROADMAP.md](ROADMAP.md) retains outstanding acceptance.

`native/manifest.json` and `native/overlay/` preserve the actual native implementation. `scripts/build-frame.cmd` verifies and applies that overlay without overwriting unexpected local changes. After deliberate edits inside the ignored Chromium checkout, run `python scripts/chromium_overlay.py export` before rebuilding. The older September `apply_*` and DEV-icon scripts are historical prototypes and are no longer invoked by the build.

The [previous Frame prototype](https://github.com/duvvy8/frame) was cloned for read-only visual inspection. Its `src/renderer/shell.css` uses a near-black base with electric blue and violet ambient colour; this first pass keeps that direction more restrained. Ember's [README](https://github.com/v11exe/ember/blob/main/README.md) and [roadmap](https://github.com/v11exe/ember/blob/main/ROADMAP.md) inform [ROADMAP.md](ROADMAP.md). Neither reference repository is modified or included in this project's build.

## September prototype history

- `scripts/apply_frame.py` changes a few Chromium product strings and the Windows executable resource's branding, and substitutes an explicitly temporary DEV icon. It leaves Chromium's browser architecture intact.
- `scripts/launch-frame.cmd` runs `Frame.exe` with `%LOCALAPPDATA%\Frame Dev\User Data`, isolated from Chrome, Edge, and default Chromium profiles. Direct `Frame.exe` launch also selects this profile through the small Chromium source overlay.
- `extensions/frame-theme` supplies a restrained dark blue and purple browser theme using Chromium's theme extension mechanism; it appeared as the active theme in Appearance settings.
- `extensions/frame-new-tab` supplies a simple new-tab page using Geist and Geist Mono. These fonts are **not yet applied to native toolbar controls**.
- `extensions/frame-sidebar` is an early Frame side panel with favourite sites, quick searches in its own address field, tab search, browser utility links, and a user-triggered Wayback lookup. It opened in Chromium's native side panel in a headed test. The panel defaults to the left, but it is not an Ember-style permanent rail yet.

The October implementation replaces the temporary DEV identity with an original Frame logo. No installer, low-memory improvement, enhanced-privacy or universal extension-compatibility claim is made.

Double-click `Open Frame.cmd` in this folder to launch the current development build with its separate profile. The output is a development build, not an installer.

## Browser layers, in plain language

| Layer | Frame file | What it does |
| --- | --- | --- |
| Chromium source and build | `CHROMIUM_REVISION`, `scripts/build-*.cmd` | Pins and compiles the complete browser, including its existing navigation and tab machinery. |
| Browser window/UI | `native/overlay/chrome/browser/ui/views`, shared WebUI resources | Native tab dock, logo, MRU bubble, split command and menu actions retain Chromium's real browser models and page ownership. Historical extension overlays are not injected. |
| Profile | `scripts/launch-frame.cmd` | Gives Frame a separate on-disk profile for settings, history, and extensions. |
| Page rendering | Chromium Blink/V8, native new-tab WebUI | Real pages retain their own fonts, selection, editing and browser isolation. Native new-tab typing uses the shared search resolver. |
| Extensions | Chromium's native extension platform | Manager enable/disable, actual request blocking and restart passed for an isolated official unpacked uBlock Origin Lite MV3 package. Store and broader compatibility remain partial. |

The active fonts are verified against the [official Geist distribution](https://github.com/vercel/geist-font). Exact source commit and licence are retained in `assets/fonts/SOURCE.md` and `Geist-OFL.txt`. Native browser-process resources and shared privileged WebUI resources bundle them offline. The September extension assets remain historical; current startup does not inject those extensions, and website/third-party popup typography is untouched.

Settings > Appearance exposes the visual recent-tab switcher and optional local measurement conversions. Select a complete measurement and right-click to copy its conversion; metric output is configurable. Ambiguous currencies, time zones, prose and unspecified fluid-ounce/gallon systems are not guessed. The rail opens real native split and recent-tab controls. ROADMAP.md distinguishes these implemented pieces from the remaining complete feature requirements.

Ctrl+PageUp and Ctrl+PageDown retain Chromium's physical tab-strip cycling. The visual Ctrl+Tab switcher uses the current window's native recency order; its actual modifier-release acceptance remains pending Windows foreground access.
