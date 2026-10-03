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

`build-frame.cmd` generates a temporary DEV icon, applies the small overlay, then rebuilds the **same** `out\FrameBaseline` directory so Chromium only recompiles affected targets. It copies the newly built `chrome.exe` to `Frame.exe` alongside its runtime files. The rebuild replaces that output's browser resources. The copied executable launched successfully on this machine and loaded `example.com`. It does not create an installer or register a default browser.

The launcher passes a dedicated profile path, `%LOCALAPPDATA%\Frame Dev\User Data`, and loads three unpacked local extensions. Direct `Frame.exe` launch also uses this profile and the bundled extension copies in `out\FrameBaseline\frame-assets`. Check `chrome://version` for the executable path, profile path, and command line. Restart with the same launcher and verify a bookmark and an open tab survive. Check that Chrome/Edge/default Chromium profiles are separate. If an extension is absent, use `chrome://extensions` → **Developer mode** → **Load unpacked** and select its directory, then record whether it remains installed after restart. A separate automated Frame profile retained local storage across restart; the normal launcher profile still needs this manual check.

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

Frame's `git diff --stat` should show only the narrow source overlay and temporary icon, unless a later deliberate change was made. The outer project does not track the giant Chromium checkout.
