# Floating dock: implementation and Windows acceptance

## Current evidence — 15 October 2026

The native overlay implements the selected reference. The cloud checkout has
neither `chromium/src`, depot_tools, a Windows SDK, nor `Frame.exe`. The complete
Views translation units have **not** been compiled, and the new browser has
**not** been launched or visually accepted. No screenshot of an updated running
browser is available. The committed concept remains a design reference only.

Available checks:

```text
python scripts/test_chromium_overlay.py
python scripts/test_frame_dock_layout.py
python scripts/check_native_manifest.py
node --check scripts/verify-frame-floating-dock.cjs
git diff --check
```

The overlay suite checks exact bytes, repeat application, conflict refusal,
revision/asset tampering, and upgrading known previous postimages without
overwriting unknown work. The layout suite compiles the actual dependency-free
C++ geometry helper and checks empty/one-tab sizing, overflow, centring, narrow
windows and caption exclusions. It does not compile Chromium or verify pixels,
native focus, platform scaling or input delivery. The manifest checker checks
all files, SHA-256 postimages, unique safe Windows paths and the pinned revision.

## Behaviour and retained controls

- The 48-DIP dock contains actual native tabs in model order, with weak tab
  identities. It reserves a 72-DIP page gutter. Its scrollable area is bounded
  by the window and a 600-DIP maximum; the plus remains outside that viewport.
  Selection and resizing reveal the active entry; scrolling to another tab
  does not activate it. An empty model retains a plus. Below one usable row,
  the dock hides until sufficient space returns; native tab search and keyboard
  commands remain available through the application menu.
- Click activates, middle click closes with native historical-tab/session
  behaviour, and right click/keyboard context menu forwards to Chromium's
  existing tab menu. Titles, domains, pin/group membership and loading, audio,
  muted and sleeping state are exposed in tooltips/accessibility. Focus reveals
  clipped entries. F6 includes the dock, split control and bottom-left menu;
  arrows traverse entries without waking sleeping pages.
- The purple dot marks selection. A small upper dot marks a pinned tab. A lower
  hollow ring marks loading, a ring with a horizontal stroke marks sleeping,
  and bars/slashed bars mark playing/muted audio. State descriptions accompany
  the marks. Favicons have a globe fallback and distinct tab identities even
  when images match.
- Frame's shell uses the dock instead of either native strip. The old
  `always_show_tabs`/`show_rail` values are retained in storage but no longer
  select layouts; their superseded Appearance toggles are removed. Disabling
  **Frame midnight-blue shell** restores conventional Chromium chrome.
- The bottom-left gear opens Chromium's existing application menu. History,
  downloads, bookmarks, settings and extensions remain there. **More tools**
  contains recent tabs, eligible sleeping, archive lookup, Copy Link and
  **Frame favourites**. That submenu retains the existing bookmark folder and
  exact-URL reuse; Ctrl-click opens a duplicate. Edit/add operate on the same
  native bookmark model. These favourites are separate from dock tabs and NTP
  shortcuts. User-entered shortcut titles remain accessible while captions
  are hidden; the existing NTP shortcut editor and drag ordering remain.
- The caption-side split control uses the existing create-split command and
  native split menu. The native omnibox, responsive extension/pinned-action
  controls and application menu remain in the compact navigation group.
  Windows caption buttons and resize testing remain native. Only toolbar
  whitespace becomes a caption hit; controls remain interactive.

## Upgrade and build an existing Windows checkout

Use cmd.exe in the outer Frame repository. First save any unsaved webpage work
and close Frame gracefully using its normal exit path, retaining its session.
Never rebuild resources or executables while Frame is running. Check local Git
status and preserve unrelated work before switching branches.

```bat
git status --short
git fetch origin
git switch feature/floating-tab-dock
python scripts/check_native_manifest.py
python scripts/chromium_overlay.py upgrade --from-ref b9d7949
call scripts\build-frame.cmd
call scripts\launch-frame.cmd
```

`upgrade` is for a pinned source checkout carrying the handoff overlay. It
preflights every file and accepts only pristine upstream bytes, the previous
commit's exact postimages, or the new postimages. It checks unchanged baseline
hashes and revisions. Any unknown edit aborts before writes. Reconcile/export
such work deliberately; do not reset it to make the command pass. If your
checkout already carries a later known overlay, use that commit as `--from-ref`.
For a fresh machine, follow [BUILD_WINDOWS.md](../../BUILD_WINDOWS.md) to install
the toolchain, fetch the pinned source and build the clean baseline, then run
`scripts\build-frame.cmd`; no upgrade step is needed for pristine source.

## Run the new native acceptance suite

Install Playwright in an ignored tools directory if it is not already available:

```bat
npm install --prefix profiles\test-tools playwright
set "PLAYWRIGHT_MODULE=%CD%\profiles\test-tools\node_modules\playwright"
node scripts\verify-frame-floating-dock.cjs
```

The script uses an isolated `profiles/dock-*` profile and local fixture pages
with Chromium's sandbox enabled. It exercises real native activation with
duplicate/missing icons, title/navigation changes, pinning, middle closure,
plus creation, many-tab overflow, actual window resizing/maximise/restore,
page-edge clickability, retained edited forms, moved Copy Link/favourite actions
and NTP caption visibility. It also records explicit pending manual checks.
For native window screenshots, install the existing capture helper's Pillow/
PyAV dependencies, set `FRAME_CAPTURE_PYTHON` to that Python executable, then
rerun. Reports and actual captures go into a new timestamped
`verification/floating-dock/` directory. Inspect the favicon-change captures
visually; accessibility data cannot prove which favicon pixels were painted.

The older October suites/report files are historical. Suites that invoke old
rail buttons directly require their action paths to be changed to the menu
routes above before rerunning; they are not proof of this redesign. The new
suite does not claim to cover all prior lifecycle/split/search acceptance.

## Finish acceptance in the real browser

1. Compare an actual new-tab window with `floating-dock-reference.png` at 100%,
   125%, 150% and 200% Windows display scale. Check restored, maximised, narrow,
   short and multi-monitor windows; verify the dock never overlaps pages,
   caption buttons or settings. Review fallback/duplicate/changed icons and
   active, loading, audio, muted and sleeping marks and hover tooltips.
2. Exercise Ctrl+T/W/Shift+T, Ctrl+1…9, Ctrl+PageUp/Down, Ctrl+Tab/reverse/release,
   F6/Shift+F6, arrows/Home/End, Enter/Space and Shift+F10. Confirm visible focus,
   active-tab scrolling, a usable plus and correct native tab context actions.
   Check that focusing/previewing a sleeping tab leaves it asleep until activation.
3. Enter a URL and a search through the native omnibox; verify suggestions,
   selection and copy/paste, `gh`, `!wiki`, `yt`, `reddit`, editable aliases and
   restart. Repeat typing and quick-search resolution in the NTP search field.
   Edit, reorder, remove and add favourite shortcuts through the existing editor.
4. Drag actual blank caption areas; double-click maximise/restore, resize every
   edge, snap, minimise, restore and use close. Check all navigation/extension
   controls remain clickable and suggestions are not clipped. Enter and exit
   fullscreen; the dock and gutter should leave and return together.
5. Create a split using the upper-right control, choose an existing tab, focus
   each pane through its real native focus path, edit forms, then use **More
   tools > Copy current page link**. It must follow the focused pane. Use the
   split menu and close one pane; the survivor's edits must remain.
6. Through native tab menus and **More tools**, check eligible sleeping, explicit
   Never sleep, active/pinned/edited/loading/download/media/capture exclusions,
   cached MRU previews, waking, session restore and persistence of Never sleep.
   Repeat the retained lifecycle tests with updated menu paths; do not infer
   protection from an icon alone.
7. Verify history/downloads/bookmark import/export/settings/extensions, selected
   measurement conversion and archive actions, including restart and an
   extension's real popup. Check third-party pages retain their own fonts and
   colours. Record results and real screenshots before calling native acceptance
   complete.

## Resume after the first Windows build failure

The first local attempt at commit `8268d6f` stopped on an obsolete navigation
header include in `app_menu_model.cc`. The two navigation includes are corrected
to the pinned revision's `chrome/browser/ui/navigator/` paths. The complete build
is still pending; the header/source audit is not compiler acceptance.

Save webpage edits and close any running Frame window, then run this single
line from the outer Frame repository to upgrade the already-applied overlay
and resume the incremental build:

```bat
git pull --ff-only && python scripts/check_native_manifest.py && python scripts/chromium_overlay.py upgrade --from-ref 8268d6f && call scripts\build-frame.cmd && call scripts\launch-frame.cmd
```

The `&&` separators stop subsequent steps when a command fails. Existing build
outputs are reused. Do not launch the previous executable during the rebuild.
