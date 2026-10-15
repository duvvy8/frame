# Windows build and launch guide

This is a source build of full Chromium, with a separate Frame output. Use an x64 Windows 10/11 machine and an **elevated installer session** for Visual Studio and the Windows SDK. Chromium's [current official Windows guide](https://chromium.googlesource.com/chromium/src/+/HEAD/docs/windows_build_instructions.md) is the authority if its requirements change.

## Requirements checked 26 September 2026

- Windows 10 or newer, x86-64, at least 8 GB RAM (more than 16 GB recommended), NTFS, at least 100 GB free. A checkout and two outputs can use substantially more; the official guide estimates its optional shared Git cache at about 30 GB and says checkout can take over an hour.
- Visual Studio **2026 (18.0+)** with **Desktop development with C++** and **MFC/ATL** components. Chromium uses clang-cl but needs Microsoft's headers, libraries, and tools.
- Windows 11 SDK **10.0.28000.2270** and SDK Debugging Tools **10.0.26100.3323+**, per the guide.
- Git for Windows and Chromium `depot_tools` on PATH ahead of other Python/Git installations. Set `DEPOT_TOOLS_WIN_TOOLCHAIN=0`.

This computer has Windows 10 Pro 19045 x64, 31.1 GiB RAM, an AMD Ryzen 7 7800X3D (8 cores/16 logical), NTFS with about 1,651 GiB free at the initial check. It has Git 2.55, Visual Studio Community **2026 18.10** with C++ and MFC/ATL, Windows SDK **10.0.28000.2526**, and Debugging Tools **10.0.28000.2526**. The initial Visual Studio 2022 installation lacked C++ tools and caused a recorded GN failure. The required toolchain was then installed; the clean baseline and Frame builds succeeded. See `PROGRESS.md` for verification results.

On a different machine, install Visual Studio Community 2026 from [Microsoft's downloads page](https://visualstudio.microsoft.com/downloads/) with the required components, then the required Windows SDK and Debugging Tools. Reboot if the installer requests it. This machine's installation completed without a requested reboot.

## Fetch and pin source

Open a new **cmd.exe** after installing the toolchain. Chromium notes that first-running `gclient` from PowerShell can miss Windows-specific setup. From the project root (the path must have no spaces):

```bat
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git depot_tools
set "PATH=%CD%\depot_tools;%PATH%"
set DEPOT_TOOLS_WIN_TOOLCHAIN=0
gclient
call scripts\prepare-source.cmd
cd chromium\src
git rev-parse HEAD
cd ..\..
```

If `depot_tools` or `chromium\src` already exists, do not reclone it. `prepare-source.cmd` fetches the full browser with Chromium's official `fetch --no-history chromium`, checks out the exact hash in `CHROMIUM_REVISION`, and runs `gclient sync -D`.

## Build and inspect a clean Chromium baseline

```bat
call scripts\build-baseline.cmd
call scripts\launch-baseline.cmd
```

In the baseline window, confirm `https://example.com` loads. Try a second site and `chrome://extensions`. Close the browser. Record the version from `chrome://version` and the navigation result in `PROGRESS.md`. On this machine, the clean baseline was built and opened `example.com` in headless mode; a normal visible baseline window was not separately checked before rebuilding its output as Frame. `build-baseline.cmd` is for a clean checkout. After the Frame overlay has been applied, repeat Frame builds with `build-frame.cmd`.

## Build and inspect Frame

The baseline must be built first because the next step edits a few files in the Chromium checkout.

```bat
call scripts\build-frame.cmd
call scripts\launch-frame.cmd
```

After a successful build, you can instead double-click `Open Frame.cmd` in the project root for subsequent launches.

`build-frame.cmd` verifies the pinned revision and applies `native/manifest.json`'s preserved source/resource overlay, then rebuilds the **same** `out\FrameBaseline` directory incrementally. It no longer runs the historical appearance/rail scripts or DEV-icon generator, which would overwrite newer native changes. It copies the newly built `chrome.exe` to `Frame.exe` alongside its runtime files. It does not create an installer or register a default browser. Before rebuilding deliberate Chromium edits, export them with `python scripts/chromium_overlay.py export`; apply refuses unexpected local changes before writing any files.

The October shell, new tab, fonts, logo, favourites and Copy Link are native Chromium/privileged WebUI work. Current startup and launcher do not inject the September prototype extensions. Their instructions below are historical rather than prerequisites for the native shell.

For reproducible checks, run `python scripts/test_chromium_overlay.py`. Run `scripts/verify-frame.cjs` using Node with Playwright installed, or set `PLAYWRIGHT_MODULE` to its module directory. It launches the actual Windows executable with the Chromium sandbox enabled and a fresh `profiles/acceptance-*` profile. Fixture websites prevent external provider responses from obscuring URL-resolution tests. Native rail and clipboard actions use Windows UI Automation, and evidence goes into `verification/2026-10-04/`. The normal user profile is never reset.

Additional native acceptance scripts cover lifecycle (`verify-frame-lifecycle.cjs`), split focus and clipboard routing (`verify-frame-split.cjs`), visual recency (`verify-frame-mru.cjs`), selected measurements (`verify-frame-conversions.cjs`), extensions (`verify-frame-extensions.cjs`), actual short windows (`verify-frame-narrow.cjs`) and recorded paint transitions (`verify-frame-transitions.cjs`). Set `FRAME_CAPTURE_PYTHON` to a Python runtime with Pillow/PyAV for window-only captures. Desktop contexts use `colorScheme:null`: Playwright's default light emulation would override the real WebUI preference and invalidate theme checks. Native actions locate the exact isolated profile window. Never run the browser while rebuilding its loaded DLL/resource files. Real shortcut checks require Windows foreground permission; their helper refuses to send keys to another application. `FRAME_MRU_UI_ONLY=1` exercises native switcher controls separately without claiming keyboard acceptance. Final acceptance writes into `verification/2026-10-05/`; October 4 evidence remains historical. `analyze_frame_transitions.py` reproduces the stated near-black crop measurement.

`verify-frame-timeout.cjs` exercises the real inactivity dropdown, Memory Saver switch and domain exclusion editor, then detaches inspection from fixture tabs while the timer runs. It can take two minutes. The lifecycle suite additionally checks actual per-tab sleep/protection and cached sleeping previews. Native tab context-menu acceptance uses Windows UI Automation 3's `ShowContextMenu`. Its test-only interop assembly is generated from the installed Windows `UIAutomationCore.dll` using the Windows SDK .NET 4.8.1 `TlbImp.exe`, and cached under ignored `profiles/test-tools`; this is not a Frame runtime dependency.

The launcher and direct `Frame.exe` launch use `%LOCALAPPDATA%\Frame Dev\User Data`. Current startup does not load the historical local extensions. Check `chrome://version` for the executable, profile and command line. The acceptance runs exercise persistence in isolated profiles; the normal profile remains untouched. Ordinary user-installed extensions still use Chromium's native installation and manager paths.

## Historical September extension prototype

The following prototype instructions describe the September implementation, not the current native NTP or rail. Retain those assets for reference; they are unnecessary for normal October startup.

For the prototype side panel, open the extensions toolbar menu, select **Frame sidebar**, and pin it if you want a persistent button. Click that button to open the panel on the left. The favourite shortcuts, tab finder, utility links, and **Archived copy** lookup are early versions; the side panel is not yet a permanent Ember-style sidebar. A normal website such as Google will still display its own page design inside Frame. The three bundled extension manifests contain public keys so their IDs remain stable across direct executable and script launches.

Chromium may show **“Change back to Google?”** when Frame's new-tab extension first takes over the new-tab page. Choose **Keep it** to use Frame's page for future new tabs. Until that one-time choice is made, a new tab can still show Google's page. This is Chromium's extension confirmation, not a failed Frame build.

Verify, in order: opening a URL; searching ordinary words in the address bar; back, forward, refresh; new and close tab; restart with the same profile; theme and new-tab display; `chrome://extensions`. Browser automation checked page navigation/history, tabs, theme, new tab, and extension management. The Windows capture helper timed out while inspecting the native window, so omnibox and native toolbar actions remain manual checks. For uBlock Origin Lite, the [official Chrome Web Store listing](https://chromewebstore.google.com/detail/ublock-origin-lite/ddkjiahejlhfcafbddmgiahcphecmpfh) offered **Install Chrome** rather than extension installation in this build. An unpacked copy from the [official release](https://github.com/uBlockOrigin/uBOL-home/releases/tag/2026.926.2202) loaded in a separate test profile and redirected one controlled ad-script request to a replacement resource. This does not mean Chrome Web Store installation works. Classic uBlock Origin is deferred because of Manifest V2 compatibility constraints.

## Useful diagnostics

```bat
cd chromium\src
git rev-parse HEAD
git diff --stat
gn args out\FrameBaseline --list
autoninja -C out\FrameBaseline chrome
out\FrameBaseline\Frame.exe --version
```

The Chromium diff contains the native Frame changes represented by `native/manifest.json`. The outer project tracks the overlay and font licences, rather than the giant Chromium checkout. Export deliberate source edits before building; never run the obsolete prototype generators over the current native implementation.

The `verify-frame-files.cjs` suite uses isolated fixture files and the actual bookmark import/export and website upload dialogs. `frame-file-dialog.ps1` accepts only a dialog owned by the exact isolated Frame window, discovers standard filename/button controls, verifies the selected filename and uses documented Windows control messages for legacy panes without UIA invocation patterns. It does not send foreground keyboard input or inspect another application.


`verify-frame-sleep-session.cjs` exercises actual native session round trips for explicit Never sleep metadata, clearing a restored preference, and independence from ephemeral extension discard flags. `verify-frame-sleep-safety.cjs` checks a real fixture download, playback, native pinned/loading/unload protections and synthetic camera/microphone capture. Its only fake-device flag supplies test media rather than physical hardware; the actual native permission button approves only the disposable local origin. After measurements, cleanup clears the isolated unload handler; a scoped native Leave fallback is available. Native helpers hide their consoles and emit UTF-8 so they do not disturb transient popup focus or corrupt Unicode accessible names.


`verify-frame-quick-searches.cjs` verifies native editing/restoration, deletion and restart, user keyword conflicts, Reddit query encoding, bare-alias homes and localhost/URL precedence through the real NTP/native models. The native engine list must be expanded before interacting with collapsed rows. Test-only permission overrides must not be used to claim persistent capture protection: disconnecting Playwright revokes them. The capture suite uses the real native permission approval with synthetic devices and checks native discard reasons after automation detaches.
