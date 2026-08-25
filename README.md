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

## Status

Migration sequencing follows the project spec.

- [x] **1. Toolchain proof** — `cefsimple` builds and renders a live page
- [x] **2. Layout constants** — ported to `src/shared/chrome_layout.h` with parity tests
- [x] **3. First OSR chrome surface** — 32px topbar rendering off-screen, driven by a message-router bridge
- [ ] 4. Sidebar, frame strips, corner masks
- [ ] 5. Real `CefBrowser` per tab
- [ ] 6. `frame://` scheme handler and internal pages
- [ ] 7. Privacy layer
- [ ] 8. Floating overlays
- [ ] 9. Extensions
- [ ] 10. Widevine
- [ ] 11. Speed pass
- [ ] 12. Telemetry verification
