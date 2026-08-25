# Frame

A custom Windows desktop browser built on **CEF (Chromium Embedded Framework) +
C++17**.

This is a ground-up rewrite of an earlier Electron implementation. The engine
change is the point: no Node.js runtime anywhere, per-tab request-context
isolation, native tracker blocking, and minimal telemetry — things the Electron
build could not guarantee by default.

> **No Node.js. No Electron. Not at build time, not at runtime.**

## Requirements

| Component | Version used |
|---|---|
| Visual Studio 2022 Build Tools | 17.14 (MSVC v143, toolset 14.44) |
| Windows SDK | 10.0.26100.0 |
| CMake | 4.4.2 (>= 3.21 required by CEF) |
| CEF binary distribution | 151.3.24 / Chromium 151.0.7922.174, `windows64`, Standard |

The CEF version is **pinned** in `scripts/get-cef.ps1` and its archive is
SHA1-verified before extraction. Bump it deliberately.

Install the toolchain:

```
winget install --id Kitware.CMake --exact
winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

## Build

```
powershell -ExecutionPolicy Bypass -File scripts\get-cef.ps1
powershell -ExecutionPolicy Bypass -File scripts\build.ps1
```

`build.ps1` configures and builds `cefsimple` (the toolchain proof), builds
Frame, and runs the test suite. `third_party/cef` and `build/` are not tracked.

## Layout

```
src/shared/     Framework-free logic shared by the browser process and every
                chrome surface. The single source of truth for layout geometry.
scripts/        Toolchain fetch, build, and run helpers.
tests/          Catch2 unit tests.
third_party/    CEF binary distribution (downloaded, untracked).
```

## Running

```
powershell -ExecutionPolicy Bypass -File scripts\run-cefsimple.ps1 -Url https://example.com
```

`run-cefsimple.ps1` opens the window on a **secondary monitor without taking
focus**, and captures screenshots via `PrintWindow` rather than by
foregrounding. Any future GUI helper should preserve that behaviour — the
primary display is often in full-screen use while builds run, and synthetic
input is avoided entirely.

## Keyboard

The chord-to-command table is `src/shared/shortcuts.h` — framework-free and
unit-tested, so a binding can be checked without starting a browser.

| | |
|---|---|
| `Ctrl+T` / `Ctrl+W` / `Ctrl+Shift+T` | new tab / close tab / reopen closed tab |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | next / previous tab (wraps) |
| `Ctrl+1`…`Ctrl+8` / `Ctrl+9` | tab by position / last tab |
| `Ctrl+N` / `Ctrl+Shift+N` / `Ctrl+Shift+W` | new window / new private window / close window |
| `Alt+←` / `Alt+→` / `Alt+Home` | back / forward / new tab page |
| `Ctrl+R`, `F5` / `Ctrl+Shift+R`, `Ctrl+F5` / `Esc` | reload / reload ignoring cache / stop |
| `Ctrl+L`, `Alt+D`, `F6` | focus the address field and select it |
| `Ctrl+B` / `Ctrl+D` | toggle sidebar / bookmark this page |
| `Ctrl+J` / `Ctrl+H` / `Ctrl+I`, `Ctrl+,` | downloads / history / settings |
| `Ctrl+0` / `Ctrl+±` | reset / adjust zoom |
| `F11` / `F12`, `Ctrl+Shift+I` / `Ctrl+P` | fullscreen / DevTools / print |
| `Ctrl+C/X/V/A/Z/Y` | the editing group — see below |

A shortcut has to work wherever the keyboard happens to be, and in Frame that
is three different places: the page is a native child window, the topbar and
sidebar are off-screen browsers fed synthesised key events, and the window
itself gets the keys when neither holds focus. All three route to one
`MainWindow::ExecuteCommand`, so a binding cannot work in one place and not
another.

The editing group is the exception, and is deliberately handled differently in
each: on a page Chromium already implements it correctly and is left alone, while
an off-screen surface routes it to `CefFrame` explicitly, because Blink's editing
shortcuts do not fire reliably for synthesised events.

Not yet bound: find-in-page and save-page, neither of which has anything behind
it yet. Binding a key to a no-op is worse than leaving it free.

## Status

Migration sequencing follows the project spec.

- [x] **1. Toolchain proof** — `cefsimple` builds and renders a live page
- [x] **2. Layout constants** — ported to `src/shared/chrome_layout.h` with parity tests
- [x] **3. First OSR chrome surface** — 32px topbar rendering off-screen, driven by a message-router bridge
- [x] **4. Sidebar and corner masks** — glass shell, antialiased viewport corners
- [x] **5. Real `CefBrowser` per tab** — tabs, navigation, reordering
- [x] **6. `frame://` scheme handler** — newtab, unreachable, and settings/history/downloads as pages awaiting their features
- [x] **6a. Keyboard commands** — the full shortcut table, multi-window, incognito
- [ ] 7. Privacy layer
- [ ] 8. Floating overlays
- [ ] 9. Extensions
- [ ] 10. Widevine
- [ ] 11. Speed pass
- [ ] 12. Telemetry verification
