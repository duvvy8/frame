# Frame — change log

A running record of what changed, when, and **why** — written for whoever picks
this up next, including a fresh Claude session with no memory of the last one.

## How to use this file

Newest entry at the TOP. One entry per working session, not per commit.

```
## DD/MM/YYYY HH:MM — one-line summary

**Branch:** name (based on X)
**Build:** passing / failing
**Tests:** N of M

### Added
### Fixed
### Changed
### Known issues / not done
### Notes for next session
```

Two rules worth keeping:

- **Say why, not just what.** "Fixed tab closing" is worth nothing in six
  weeks. "CEF's default `DoClose` posts WM_CLOSE to the top-level parent" is
  the thing that stops someone reintroducing it.
- **Record what is NOT done.** The gaps are the part a new session cannot
  discover by reading the code, because absent features leave no trace.

---

## 26/08/2026 16:35 – 20:23 — production audit: tab lifecycle, context menu, sleep, real internal pages, tooltips, find

**Branch:** `audit/production-pass` — pushed, awaiting review
**Based on:** `feat/shortcuts-and-landing` (NOT `main` — see Notes)
**Build:** passing
**Tests:** 147 of 147 (51 Catch2 unit + 96 behavioural across 8 suites)

Six commits, `acfa773` → `dabb8e5`.

### The three reported bugs

All three reproduced first, then fixed at the root, then re-verified.

**Closing any tab closed the whole browser.** Page browsers are created with
`SetAsChild`, so they are windowed, and CEF documents what the default does in
that case: *"returning false from DoClose() will send the standard close
notification to the browser's top-level parent window (e.g. WM_CLOSE on
Windows)"*. `PageClient` did not override `DoClose`, so every tab that closed
posted `WM_CLOSE` to Frame's window. By the book, not by accident.

Returning `true` alone is WORSE than the bug — CEF then expects the client to
"complete the browser close ... by proceeding with window hierarchy tear-down",
so the window stopped closing and the *tab* stopped closing too. `DoClose` now
returns true **and destroys the browser's own child window**.

**Ctrl+W could take the browser down.** Same path. Nothing to do with the key.

**The New Tab button stopped working once enough tabs were open.** It never
stopped working — it stopped being anywhere you could click. The `+` shared a
flex line with the tabs, tabs cannot shrink past their minimum, so the line
overflowed and carried the button under the caption buttons and eventually off
the window. It is now a sibling of a scrolling tab list.

### Fixed

- **Use-after-free on window close.** `Detach()` was declared on both CEF
  clients and called by NOTHING; `WM_CLOSE` fell through to `DefWindowProc`,
  destroying the window while every browser was still alive. Closing is now
  two-phase (`BeginClose` / `MaybeFinishClose`), and clients hold a revocable
  `WindowRef` instead of a raw pointer.
- **Middle-click never reached the tab strip.** The topbar had handled button 1
  since tabs existed, but `WM_MBUTTONDOWN` was never forwarded to it.
- **Clicking a tab that had ever been active did nothing.** The mousedown
  handler captured `isActive` at build time, and tabs are reconciled in place
  rather than rebuilt. Most visibly: sleeping tabs could not be woken.
- **A newly opened tab landed off-screen on a full strip.** A tab enters by
  growing from zero width, so "keep the active tab visible" ran while it still
  had none.
- **A sleeping tab promoted to active by a close was not woken** — blank
  viewport with nothing to explain it.
- **History triple-counted every visit** ("3×" for a page opened once). One
  navigation produces both an address change and a title change; both were
  being recorded as visits.
- **Tooltips never appeared at all.** CEF: *"When window rendering is disabled
  the application is responsible for drawing tooltips and the return value is
  ignored."* Frame's chrome is windowless, so its ten `title` attributes drew
  nothing.
- **The test harness reported a green run over three dead suites.** Each
  recorded 0-of-0, which sums perfectly and means nothing.

### Added

- **Tab context menu** (12 items). Its own owned top-level layered window,
  because the page carries `WS_EX_NOREDIRECTIONBITMAP` and nothing composited
  beneath or beside it can draw over it. Content is an off-screen browser whose
  bitmap goes to `UpdateLayeredWindow`, so corners and shadow are real.
- **Sleep Tab.** A sleeping tab has NO renderer process — `WasHidden()` and
  audio muting were both considered and both leave the process running.
  Verified: live page renderers 2 → 1. Policy lives in
  `src/shared/sleep_policy.h`, framework-free and unit-tested, because the
  failure mode is losing what someone typed into a form. Audio and unsaved
  input are probed in the page before a tab is discarded.
- **Real internal pages.** settings / history / downloads / bookmarks were
  documents explaining what would eventually go there. They now read and write
  real stores under the profile directory, tab-separated and editable by hand.
- **`CefDownloadHandler`.** Frame had none, which is why downloads were
  DECLINED rather than saved.
- **Tooltips**, drawn by Frame (see above). Same `MenuSurface` class as the
  menu, pointed at a different page and made click-through.
- **Find in page** (`Ctrl+F`, `F3`). Chromium's own search, so the highlighting
  and match count are real. The field is in the SIDEBAR, not floating — see
  Known issues.
- **Mouse side buttons** navigate back / forward.
- **A synthetic-input test harness** (`scripts/drive.ps1` + 8 suites). Drives a
  real Frame through posted window messages; never moves the physical cursor.

### Changed

- Tab strip scrolls when crowded; tabs shrink to a real floor
  (`kTabFloorWidth`) before it does; the active tab is kept in view.
- Settings apply to EVERY open window, not just the one they were clicked in.
- The tooltip popup registers `FrameTooltipSurface`, the menu
  `FrameMenuSurface` — two things that behave differently should not be
  indistinguishable to the platform.

### Known issues / not done

- **60 FPS is verified structurally, not measured.** Transitions were checked
  to stay off layout properties and hidden surfaces stop rendering. No frame
  profiler was run. Do not claim a number.
- **Not built:** tab pinning, session restore on launch, download
  pause/cancel/retry, "ask where to save".
- **The find bar is in the sidebar**, not a floating overlay. A floating one
  needs an overlay that can accept typing — migration step 8.
- **The sleep probe trusts the page** about audio and unsaved input. A page can
  lie to stay awake (it could anyway, by playing a silent sound).
- **Deliberately NOT ported from Ember:** extensions, unit conversions, bang
  shortcuts, archive lookup, upload panels, tab thumbnails, the switcher.
  Product features rather than browser fundamentals, several tied to Electron
  machinery.
- **Migration steps 7–12 untouched:** privacy layer (per-tab request contexts),
  floating overlays, extensions, Widevine, speed pass, telemetry verification.

### Notes for next session

- **This branch is based on `feat/shortcuts-and-landing`, not `main`.** That
  branch had 10 unmerged commits (keyboard commands, tracker blocking,
  fingerprint guard, the Unbounded font, the new-tab gradient). The reported
  bugs only existed there — `main` has no keyboard shortcuts at all, so Ctrl+W
  could not have crashed it. Merging this to `main` brings both sets.
- **Run the tests before believing anything:**
  `powershell -ExecutionPolicy Bypass -File scripts\test-all.ps1`
- `scripts/test-real-pointer.ps1` is NOT in `test-all.ps1` because it moves the
  physical cursor. It answers the two questions synthetic input cannot: whether
  hover works through Frame's own routing, and whether tooltips appear.
- **Two harness limits are Windows', not Frame's**, and both are written down
  in `scripts/drive.ps1`: posted chords carry no modifiers (`GetKeyState` reads
  the real keyboard), and posted hover is cleared immediately
  (`TrackMouseEvent` watches the real cursor). Both are worked around through
  DevTools. Do not "fix" the browser for either.
- **Never open a browser from inside a CEF callback.** The tooltip crashed the
  browser process this way for one build — `CreateBrowser` from inside
  `OnTooltip` re-enters CEF and faults in `libcef`. `OnLoadError` already
  documents the same hazard. Post it to a timer.
- `claude-mem` was down for this whole session (`Not logged in`), so none of it
  was remembered. This file is the handover.
