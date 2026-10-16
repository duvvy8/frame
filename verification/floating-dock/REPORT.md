# Floating dock: local Windows evidence, 15–16 October 2026

The native browser builds and runs. Five visual defects were found and corrected
against the selected reference. **Full acceptance remains incomplete** because
Computer Use capture/pointer geometry is unavailable, and the broader platform
and retained-feature matrices have not been completed.

## Changes and design review

Frame's purpose remains a quiet browser around real web content, with its small
floating tab dock as the signature element. The selected navy palette, Geist,
overlapping-square mark and solid surfaces remain. No blur, glass material,
decorative gradients or extra panels were added.

The user-requested `apple-design` skill was installed into
`C:/Users/Michael/.codex/skills/apple-design` from
[`dickwu/apple-design-skill`](https://github.com/dickwu/apple-design-skill), pinned
to `914b6a377c22755597e4405e54911b996de822d7`. Its general design principles were
translated to Chromium Views and Windows conventions. This is not a claim of
Apple platform compliance, and the skill is not shipped with Frame.

| Finding | Correction and verification |
| --- | --- |
| High: the native top container painted over the Frame mark and caption split control. | Create floating controls after that container. Real captures show both; F6 reaches the split control and Enter opens the existing native chooser. |
| Medium: dock favicons and custom button images used ImageButton's top-left default. | Centre images horizontally and vertically within the existing targets. Duplicate and fallback icons were visually inspected. |
| High: dock focus was clipped to a partial line. | Inset native focus rings by 2 DIPs. F6/arrow captures show complete rings; arrows retain the current page, Enter activates the focused tab. |
| High: Add shortcut inherited a 71.95px text-button minimum inside a 60px tile. | Scope `min-width: 0` to the existing icon-only component mode. Before: 120px scroll height inside a 60px viewport, with focus scrolling 60px and hiding favicons. After: button width 60px, scroll height 60px, viewport 60px, scrollTop 0. |
| High: a short NTP put shortcut targets beneath the fixed customise control. | At short heights, tighten wordmark spacing and reserve a 72px footer spacer. At an actual 780×420 OS window the shortcut row clears the customise target by 12.125px. The shortcut dialog remains usable. |

These choices follow the installed references `layout.md › Visual hierarchy`
(“Align elements”), `accessibility.md › Mobility` (control size and spacing),
`focus-and-selection.md › Best practices` (visible platform focus) and
`toolbars.md › Best practices` (avoid overcrowding). Keeping solid navy surfaces
is the user's explicit constraint. Windows caption controls, native keyboard
commands, native menus and third-party page styling remain the implementation
boundary. No new colour-contrast or OS text-scale claim is made.

## Build and source checks

- The handoff Windows build was reported successful in 98 steps.
- Both local native control/focus correction builds passed in 3 steps.
- The shortcut CSS/resource build passed in 26 steps.
- The final short-window CSS/resource build passed in 11 steps. Generated CSS
  was checked to contain the final cascade order.
- Four overlay regression tests passed; the actual dependency-free C++ geometry
  test compiled and ran with the bundled clang compiler and installed VS runtime.
- Manifest integrity passed for 131 files. Every existing baseline hash and
  `CHROMIUM_REVISION` remain unchanged. The new shared CSS baseline comes from
  the exact pinned Git object; working-tree CRLF conversion was accounted for.
- `node --check scripts/verify-frame-floating-dock.cjs` and `git diff --check`
  passed. The harness gained wrapping/focus-scroll/footer-clearance assertions.
  **The complete native harness was not run.** Targeted live browser checks
  exercised the new geometry assertions separately.

The preserved local build logs are `2026-10-16-local/build.log` and
`2026-10-16-local/build-short-window.log` (ignored build logs, not browser files).
Raw incremental results are in
[15 October acceptance](2026-10-15-local/acceptance.json) and
[16 October acceptance](2026-10-16-local/acceptance.json). Earlier failed attempts
and test-session recovery notes are retained as history. The
[final acceptance summary](acceptance-summary.json) separates successful checks,
resolved intermediate failures and outstanding acceptance.

## Actual runtime results

The real `chromium/src/out/FrameBaseline/Frame.exe` ran with Chromium's sandbox,
isolated test profiles and no colour-scheme emulation. Local HTTP fixtures and
Playwright supplied test data, renderer assertions and actual OS window bounds
through CDP. Native keyboard interaction used the Computer Use `@oai/sky` API.
No mock browser chrome or injected replacement interface was used.

| Workflow | Result and limit |
| --- | --- |
| Native dock model | Three distinct real tabs, duplicate/fallback favicons, model order and pinning passed. |
| Focus and activation | F6, arrows without activation, Enter activation, caption split focus and bottom-left gear focus passed with visible rings. |
| Context menus | Shift+F10 exposed Chromium's tab menu, including disabled active-tab sleep and Never sleep; Pin updated native order. Physical right-click remains untested. |
| Tab lifecycle commands | Ctrl+T, Ctrl+W and controlled Ctrl+Shift+T website restoration passed; edited neighbouring form retained. An earlier blank-NTP restore reached prior test history and did not establish single-tab restoration. |
| Navigation | Native omnibox typing, URL suggestion display, Enter navigation, Alt+Left/Right and Ctrl+R passed. Search providers/alias editor were not rerun. |
| Split view | Keyboard caption activation, existing-tab chooser, two live 567px panes, independent form retention and Separate views passed. |
| Moved Copy Link | Native More tools action copied the focused right pane's exact URL. Previous clipboard text was restored. Broader focus-routing matrix remains pending. |
| Frame favourites | Add through the native submenu and exact-URL tab reuse passed, retaining both edited forms. |
| Overflow and windows | 26 real tabs, 780×420 OS bounds, Ctrl+1/9 reveal, maximise/restore through CDP passed. This does not establish pointer resize/drag/Snap. |
| Fullscreen | F11 removed dock/gutter; renderer width became 2560px. Exit restored dock/gutter and 680px content in the short window. |
| Existing WebUIs | Appearance, History, Downloads, Bookmarks and Extensions loaded and were visually inspected. Manager loading does not prove extension popup or import/export behaviour. |
| Website styling | Fixture retained serif, white background and black text. |
| NTP shortcuts | Accessible title retained with captions hidden; actual keyboard name/URL entry and save passed. After correction, focus/editor/cancel and short-window clearance passed. |

The original user profile was not reset. The initially opened regular window had
only an empty NTP and was closed gracefully before rebuilding. Test windows were
closed gracefully; known test-only edited forms were not user work. The pinned
source and unrelated local changes were preserved.
The regular profile was reopened after the final build and left running on its
existing NTP. Its final screenshot was inspected; shortcut icons, the dock,
caption controls and settings are visible without clipped targets.

## Screenshot index and visual QA

All saved PNGs were inspected for spacing, alignment, clipping, colours,
readability and workflow state. They are actual native-window or screen-region
captures. Some intermediate PrintWindow captures omit composited siblings or
popup windows; some restored-window screen crops cut off menus extending outside
the browser bounds. Those images are retained as intermediate evidence, not
accepted full-menu captures. The maximised complete menu images below resolve
that capture limitation. Tiny desktop/window-border slivers and the computer
control cursor indicator can be present in screen-region captures.

- [Final regular NTP](2026-10-16-local/final-new-tab.png)
- [Regular profile left open after testing](2026-10-16-local/regular-profile-final.png)
- [Final short NTP and footer clearance](2026-10-16-local/final-short-ntp.png)
- [Final short NTP focus](2026-10-16-local/final-short-ntp-focus.png)
- [Final short-window shortcut dialog](2026-10-16-local/final-short-shortcut-editor.png)
- [Complete More tools menu](2026-10-16-local/complete-more-tools-menu.png)
- [Complete favourites submenu](2026-10-16-local/complete-favourites-menu.png)
- [Favourite reuse with edited form](2026-10-16-local/favourite-reused.png)
- [Closed website restored](2026-10-16-local/website-restored.png)
- [Corrected native dock focus](2026-10-15-local/final-focus-dock.png)
- [Keyboard focus without activation](2026-10-15-local/final-focus-next.png)
- [Real split panes](2026-10-15-local/final-two-pane-split.png)
- [Native split management menu](2026-10-15-local/final-split-menu.png)
- [Bottom-left gear menu](2026-10-15-local/final-gear-menu.png)
- [Short-window overflow](2026-10-15-local/final-overflow-last.png)
- [Fullscreen](2026-10-15-local/final-fullscreen.png)
- [Native omnibox suggestions](2026-10-15-local/final-omnibox-suggestions.png)
- [Appearance settings](2026-10-16-local/settings-appearance.png),
  [History](2026-10-16-local/history.png),
  [Downloads](2026-10-16-local/downloads.png),
  [Bookmarks](2026-10-16-local/bookmarks.png),
  [Extensions](2026-10-16-local/extensions.png)

The 15 October `final-*` prefix identifies the native-control iteration; it does
not supersede 16 October NTP fixes. Pre-fix captures remain useful reproductions:
`before-new-tab.png`, `focus-dock.png`, `shortcut-saved-settled.png` and
`fixed-shortcuts-short-window.png` show the defects subsequently corrected.

## Blocked and remaining acceptance

Computer Use screenshot capture failed twice on 15 October and again after a
fresh session on 16 October: `FrameArrived timed out: timed out waiting on
channel`, then `window capture timed out: timed out waiting on channel` after
fresh target selection. Pointer activation returned `coordinate input geometry
is unavailable`. Recovery did not restore that capability. Accessibility reads
and real keyboard input worked; fallback native captures supplied the images.

Full acceptance stays open for pointer activation/middle-click/right-click,
hover/tooltips, wheel scrolling, page-edge hit delivery, caption dragging,
all-edge resizing, Snap, multi-monitor and 100/125/150/200% Windows display scales.
Also pending are a full lifecycle/media/capture/download protection rerun,
sleeping preview/wake/session persistence, complete aliases/search/IME,
extension popup, bookmark import/export, archive and conversion regressions.
These are not inferred from historical October reports, a passing build, an
accessible name or a state icon.
