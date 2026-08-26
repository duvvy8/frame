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
                chrome surface. The single source of truth for layout geometry,
                the shortcut table, the frame:// allowlist, the tracker
                matcher, and the sleep policy. No CEF, no Win32, no platform
                headers — so all of it is unit-testable without a browser.
src/browser/    The browser process: the native window, the tab lifecycle, the
                off-screen chrome surfaces, the popup menu surface, the
                frame:// scheme handler, and the stores behind the internal
                pages.
src/renderer/   Every surface and internal page, as ordinary HTML/CSS/JS served
                over frame://. Staged next to the executable at build time, so
                editing one needs a rebuild but not a reconfigure.
scripts/        Toolchain fetch, build and run helpers, plus the synthetic-input
                test harness — see Testing.
tests/          Catch2 unit tests over src/shared/.
third_party/    CEF binary distribution (downloaded, untracked).
```

Three kinds of surface, and the difference matters:

- **The page** is a real child `CefBrowser` window. CEF owns its input, focus,
  scrolling and IME, so none of that is reimplemented.
- **The topbar and sidebar** are off-screen browsers. Frame owns their pixels
  and composites them in `WM_PAINT`, which is what lets the window be assembled
  from independently rendered pieces.
- **The context menu and the corner masks** are owned top-level layered
  windows. They have to be: the page carries `WS_EX_NOREDIRECTIONBITMAP`, so
  nothing composited beneath or beside it can draw over it, and an owned
  top-level window is the only layer DWM puts above it.

## Running

```
powershell -ExecutionPolicy Bypass -File scripts\run-cefsimple.ps1 -Url https://example.com
```

`run-cefsimple.ps1` opens the window on a **secondary monitor without taking
focus**, and captures screenshots via `PrintWindow` rather than by
foregrounding. Every GUI helper here preserves that: the primary display is
often in full-screen use while builds run.

## Testing

Two layers, and they check different things.

`tests/` is Catch2 over `src/shared/` — the framework-free logic where a
mistake is silent and expensive: the chord table, the layout geometry, the
`frame://` allowlist, the tracker matcher, the sleep policy. None of it needs a
browser, so all of it runs in a quarter of a second.

`scripts/test-*.ps1` drive a **real Frame**, because most of what a browser
gets wrong cannot be seen from a unit test: a control that is never reachable,
a menu that renders nothing, a setting that saves and changes nothing, a tab
that closes the window with it.

```
powershell -ExecutionPolicy Bypass -File scripts\test-all.ps1
```

| | |
|---|---|
| `repro-tabs.ps1` | the tab-lifecycle bugs: closing a tab, New Tab, `Ctrl+W` |
| `test-menu.ps1` | the tab context menu, and sleep actually freeing a renderer |
| `test-pages.ps1` | settings, history, downloads and favourites doing real work |
| `test-settings-effect.ps1` | each switch changing behaviour, not just the file |
| `test-shortcuts.ps1` | every command pressed, and its effect observed |
| `test-hover.ps1` | hover and transition states across every surface |
| `test-stress.ps1` | twenty tabs, rapid churn, renderer and memory behaviour |

`scripts/drive.ps1` is the harness they share. It is worth knowing how it
works before adding to it:

- **Input is posted window messages.** `PostMessage(WM_LBUTTONDOWN, …)` to the
  browser window enters Frame's own `WndProc`, which is the same path a real
  click takes — one step earlier, and without moving the physical cursor.
- **Geometry comes from the surfaces themselves**, over the DevTools protocol,
  rather than being predicted from the layout constants. A click lands on the
  control even after the CSS moves it.
- **Chords go through DevTools**, not posted messages. Frame reads Ctrl and
  Shift with `GetKeyState`, which reports the real keyboard — a posted
  `WM_KEYDOWN` for `VK_CONTROL` never enters it, so a posted chord arrives with
  no modifiers. That is a limit of the harness, not of the browser.
- **Hover on an off-screen surface goes through DevTools too**, for a similar
  reason: `TrackMouseEvent` watches the real cursor, so with the pointer on
  another monitor Windows posts `WM_MOUSELEAVE` immediately and Frame correctly
  clears the hover a synthetic move had just set.

Frame has to be started with `--remote-debugging-port` for any of the
inspection to answer; `Start-Frame` does that.

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
| `Ctrl+J` / `Ctrl+H` / `Ctrl+I`, `Ctrl+,` / `Ctrl+Shift+O` | downloads / history / settings / favourites |
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
- [x] **6. `frame://` scheme handler** — newtab, unreachable, settings, history, downloads, favourites, and the context menu
- [x] **6a. Keyboard commands** — the full shortcut table, multi-window, incognito
- [x] **6b. Tab lifecycle** — see below; closing a tab no longer closes the browser
- [x] **6c. Tab context menu** — its own layered popup, above the page
- [x] **6d. Sleep Tab** — a background tab's renderer is discarded, not throttled
- [ ] 7. Privacy layer

### Tab lifecycle

Three symptoms, one root cause and one consequence of fixing it, all worth
recording because none of them is guessable from the code that was there.

**Closing any tab closed the whole browser.** A page browser is created with
`SetAsChild`, so it is windowed, and CEF documents what the default `DoClose`
does in that case: *"returning false from DoClose() will send the standard
close notification to the browser's top-level parent window (e.g. WM_CLOSE on
Windows)"*. `PageClient` did not override `DoClose`, so every tab that closed
posted `WM_CLOSE` to Frame's window — by the book, not by accident. That
notification is for applications whose window *is* the browser; a tab is not a
window.

Returning `true` alone is worse than the bug: CEF's contract is that a client
returning true must "complete the browser close … by proceeding with window
hierarchy tear-down", so the window stopped closing and the *tab* stopped
closing too. `PageClient::DoClose` returns true **and destroys the browser's
own child window**, which is the tear-down CEF is waiting for.

**The New Tab button stopped working once enough tabs were open.** It did not
stop working; it stopped being anywhere you could click. The `+` shared a flex
line with the tabs, tabs cannot shrink past their minimum, so the line
overflowed and carried the button under the caption buttons and eventually off
the window. It is now a sibling of a scrolling tab list, and no number of tabs
can move it.

**Ctrl+W could take the browser down.** The same `DoClose` path — nothing to do
with the key.

**Window teardown was a use-after-free.** `WM_CLOSE` fell through to
`DefWindowProc`, which destroys the window immediately while every page and
surface browser is still alive, so CEF went on calling back into a `MainWindow`
already queued for deletion. Closing is now two-phase — see `BeginClose` — and
every client holds a revocable `WindowRef` instead of a raw pointer.

### Sleep Tab

A sleeping tab has **no renderer process**. Not a throttled one: `WasHidden()`
and audio muting were both considered and both leave the process running, which
means they leave essentially all of the memory allocated. Frame closes the
browser and remembers the URL, which is what Chromium's own tab discarding
does, and it is the only version of this feature that can honestly claim to
free anything. Measured on a two-tab window: one renderer process gone, ~50MB
returned.

The cost is the page's in-memory state, so `src/shared/sleep_policy.h` decides
what may sleep — and it is framework-free and unit-tested, because the failure
mode is losing what someone typed into a form. Audio and unsaved input are
facts only the renderer has, so a candidate tab is probed in the page before it
is discarded.
- [ ] 8. Floating overlays
- [ ] 9. Extensions
- [ ] 10. Widevine
- [ ] 11. Speed pass
- [ ] 12. Telemetry verification
