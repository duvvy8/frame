# Frame milestone 1 progress — 27 September 2026

## 15 October: first Windows redesign build failure and header correction

The first reported local Windows build generated its targets, then stopped
compiling `app_menu_model.cc`: the two new navigation includes used the obsolete
`chrome/browser/ui/` location. Both now use the pinned revision's
`chrome/browser/ui/navigator/` paths. A source audit checked all 17 quoted includes
introduced by the redesign against the overlay, exact pinned upstream sources,
or a verified `.mojom` generator source. The four overlay regression tests,
compiled layout checks and 130-file manifest validation still pass. This fixes
the reported missing-header error; a successful complete Windows build and
runtime/visual acceptance are still pending. The prior executable was retained.
For a checkout carrying the first redesign overlay, upgrade from `8268d6f`
before resuming the incremental build; unknown local source edits still refuse
writes. See the redesign verification document for the single-line command.


## 15 October: floating-dock source implementation (Windows acceptance pending)

Implemented the selected concept in native Views and the browser-owned NTP.
The 48-DIP floating dock uses weak native TabInterface identities, model order,
actual favicon updates, native activation/historical closure and existing tab
context menus. It bounds its viewport, retains a fixed plus, reveals selected/
focused tabs and reserves a page gutter. Tooltips/accessibility expose title,
domain, pin/group and loading/audio/muted/sleeping state. F6 includes the dock,
caption-side split and menu controls; arrow focus does not activate tabs.

The compact toolbar retains the native omnibox and responsive extension/actions
machinery. Caption exclusions and non-client hit testing preserve native Windows
controls and whitespace dragging. The fixed Frame mark and bottom-left gear are
separate from the scroll viewport. The gear opens Chromium's existing app menu;
More tools retains recent switching, safe batch sleeping, archive lookup, Copy
Link and bookmark-backed Frame favourites with exact-URL reuse. NTP shortcuts
remain a separate native component with hidden captions and existing editing.
Superseded rail/tab-strip toggles are removed while their stored prefs remain;
disabling Frame shell restores conventional chrome.

Added guarded overlay upgrade from a prior committed manifest so an existing
Windows checkout need not reset its old postimages. The new regression confirms
that a single unknown local edit refuses all writes and that a recognised
upgrade is byte-exact and repeatable. Four overlay regression tests and the
compiled C++ layout invariant test pass. The full manifest is checked for safe
unique paths, existing exact-byte postimages, SHA-256 and pinned-revision agreement.
New upstream-existing menu files use baseline bytes fetched at that revision;
existing before hashes and CHROMIUM_REVISION are unchanged. The Windows suite
has passed JavaScript syntax checking only; no native acceptance has run here.

This Linux cloud checkout has no full Chromium source, Windows SDK or executable.
The complete native translation units are uncompiled; runtime, visual, platform
focus/caption/scaling, split and sleeping/session acceptance remain pending.
No updated browser screenshot was produced. Exact safe upgrade/build commands,
a new native dock acceptance harness and the remaining checklist are in
[docs/design/floating-dock-verification.md](docs/design/floating-dock-verification.md).
Earlier screenshots/reports do not establish redesign acceptance.


## Build and environment

- This is a separate Git project at `C:\Users\Michael\Documents\frame`. The previous Frame and Ember repositories were inspected in ignored `research/` copies; neither reference repository was changed.
- The full Chromium source was fetched through official `depot_tools` and pinned to `ad04fe860307f4c408ca74ef7dbfcaaa221cff85` in `CHROMIUM_REVISION`. The build reports Chromium `156.0.8076.0` (Developer Build, 64-bit). `chrome://version` shows an all-zero revision field in this developer build, so use the pinned Git hash for source identification.
- Host: Windows 10 Pro 19045 x64, Ryzen 7 7800X3D (8 cores, 16 logical), about 31.1 GiB RAM, NTFS with about 1,651 GiB initially free. Toolchain: Visual Studio Community 2026 18.10 with Desktop C++, MFC/ATL, Windows SDK and Debugging Tools 10.0.28000.2526. Chromium's [Windows build guide](https://chromium.googlesource.com/chromium/src/+/HEAD/docs/windows_build_instructions.md) calls for Windows 10+, 8 GiB minimum, 100 GiB+ free, Visual Studio 2026, and the current Windows SDK/debugging tools.
- The first GN attempt failed because the existing Visual Studio 2022 lacked VC tools. Installing the required 2026 toolchain fixed it. GN generated 36,195 targets. A later Windows shutdown interrupted compilation; an incremental restart retained prior work.
- The clean baseline build completed successfully (`baseline-build.exit=0`; final resumed log: `Build Succeeded: 20504 steps`). The baseline `chrome.exe` opened `https://example.com` in headless mode and returned its `Example Domain` page. A normal visible baseline window was not separately checked before its output was reused for Frame.
- `scripts\build-frame.cmd` applied the narrow source branding and temporary DEV icon, then successfully rebuilt the same output incrementally (`frame-build.exit=0`; final log: `Build Succeeded: 559 steps`). `chromium\src\out\FrameBaseline\Frame.exe` exists. The output directory now contains Frame-branded resources, so `chrome.exe` there is no longer a clean baseline snapshot.

## Frame checks that passed

- `Frame.exe` started in headless mode, loaded `https://example.com`, and created its specified test profile. `scripts\launch-frame.cmd` opened a normal window titled **Frame** with `%LOCALAPPDATA%\Frame Dev\User Data` on its command line; that directory was created. The ordinary launcher window remains open for inspection.
- A separate automated Frame window loaded `example.com` and an IANA page. Page navigation back, forward, and reload returned the expected URLs and titles. A second tab opened and closed. These checks used Chromium's existing browser implementation through Playwright; they did not exercise clicks on the native toolbar or typing in the omnibox.
- `chrome://version` displayed **Frame**, the `Frame.exe` path, and the isolated test profile path. A value written to `example.com` local storage survived closing and reopening Frame with the same test profile.
- `chrome://newtab/` opened Frame's bundled new-tab extension. A screenshot showed the restrained dark blue/purple page. Browser font inspection reported Geist, IBM Plex Sans, and IBM Plex Mono loaded. `chrome://settings/appearance` identified **Frame development theme** as the active theme. Native Chromium toolbar fonts were not changed.
- `chrome://extensions/` opened and showed the Frame new-tab extension enabled. The Chrome Web Store's uBlock Origin Lite page displayed **“Switch to Chrome to install extensions and themes”** in this Chromium build, so direct Web Store installation was unavailable in that check.
- Downloaded the official `uBOLite_2026.926.2202.chromium.zip` from [uBlock Origin Lite's GitHub release](https://github.com/uBlockOrigin/uBOL-home/releases/tag/2026.926.2202) into an ignored test directory. A separate Frame test profile loaded it unpacked; `chrome://extensions/` showed **uBlock Origin Lite** enabled. In a controlled request from `example.com`, an ad script URL was redirected to the extension's replacement resource (HTTP 307); the same request returned HTTP 200 without uBOL. This confirms one filter action, not broad ad blocking performance or permanent store installation.

## Limits and remaining checks

- The Windows Computer Use helper timed out twice while capturing the visible Frame window (`FrameArrived timed out`, then `window capture timed out`). The process and title were observed, but native toolbar buttons, omnibox URL/search entry, and the appearance of browser chrome were not directly inspected. These are Chromium components that Frame has not modified, but their behavior should still be manually checked before claiming full interactive acceptance.
- The normal launcher profile was observed on the command line and disk; restart persistence was verified with a separate automated profile, not by manually restarting the normal launcher.
- The uBOL test was an unpacked test profile using a downloaded official release. Frame's regular launcher does not bundle uBOL. Chrome Web Store installation remains unsupported in the observed store UI.
- No installer, logo, low-RAM claim, or improved-privacy claim has been made or tested.

## 27 September continuation: product identity and sidebar prototype

- Patched Chromium's Windows profile path and installation identity so opening `Frame.exe` directly selects `%LOCALAPPDATA%\Frame Dev\User Data`. A direct visible launch updated that profile's `Local State` and `Preferences`; the default Chromium profile timestamp did not change. The app still has Chromium-looking native controls; the latest main window title was `New tab · Frame - Chromium`. Full native UI branding remains open.
- Direct launch now loads bundled local extensions and suppresses Chromium's default-browser check. A headless direct `Frame.exe` launch returned Frame's new-tab HTML. In a read-only desktop screenshot, the toolbar was purple and a Frame new-tab page was visible behind Chromium's new-tab override confirmation. A forced stop also caused Chromium's normal restore-pages prompt. These prompts are not claimed fixed.
- Added a first Frame sidebar extension with Geist and Geist Mono, favourite shortcuts, an address/search field with four fixed aliases, open-tab search, and Bookmarks/History/Downloads/Extensions links. Its action is configured to open Chromium's native side panel when clicked; the actual side panel placement and action click have **not** been visually verified.
- Incremental build after the sidebar source change succeeded (`frame-build.exit=0`, one link step). The first attempt failed because a still-running Frame process held `chrome.dll`; stopping that process and rebuilding succeeded. `Frame.exe` and the sidebar assets are present in `out\FrameBaseline`.
- In a separate headed Playwright profile, `chrome://extensions` showed **Frame sidebar** enabled. Its `panel.html` rendered without console errors. Clicking GitHub opened one GitHub tab; clicking it again reused that tab. Searching open tabs found GitHub. Entering `yt chromium browser` in the sidebar field opened YouTube results. Its Extensions utility button opened `chrome://extensions`. These checks exercised the extension page as a tab, not its native side panel container.
- A screenshot of the sidebar page showed the chosen indigo-to-sapphire gradient and Geist typography. A normal website, including Google, still renders its own design inside Frame. Native toolbar redesign and most Ember features remain unfinished.
- A later direct visible launch showed a Google new-tab page in the first tab. Opening Frame's extension page directly showed the Frame new-tab design, alongside Chromium's **“Change back to Google?”** prompt with **Keep it** and a restore-pages prompt caused by the earlier forced process stop. Frame's extension is present, but its override is still awaiting that user choice in the regular profile. This is the concrete reason the current browser can still look like Google on a new tab.

## Source and next action

The outer project contains scripts, the temporary DEV icon, three local extensions, bundled fonts/licenses, and documentation. The ignored Chromium checkout has the Frame branding, profile, and extension startup overlay. The single next milestone is **native Frame window and side panel integration**: make the Frame sidebar discoverable in the actual browser window, verify its action opens the panel, inspect native controls, and improve their visual identity without disturbing Chromium's normal navigation and extension behaviour.

## Later 27 September continuation

- Corrected Chromium's separate Windows window-title format. After a successful incremental rebuild, the visible Frame process reported `New tab - Frame` rather than `New tab · Frame - Chromium`.
- Added public manifest keys to all three bundled extensions. In separate headed test profiles, the sidebar had the same ID, `kfakghhnllkfncapopkldjaocfooilkm`, whether loaded from copied output assets or the source extension path. This avoids extension identity changing with the launch method.
- Changed the default Chromium side-panel alignment to the leading edge. A headed test opened the Frame extension in Chromium's actual side-panel container, and a desktop screenshot showed the Frame panel on the **left** with its favourites and tools visible.
- An attempt to pin the sidebar toolbar action at startup crashed Frame in a fresh-profile run. That source change was removed. The recovery build succeeded and a fresh headed Frame profile opened `chrome://extensions` with the Frame sidebar enabled. The sidebar action still needs to be selected or pinned through Chromium's extensions menu.

## 4 October: native baseline recovery and browser fixes

The October checkout is a native Chromium 156.0.8076.0 fork at `ad04fe860307f4c408ca74ef7dbfcaaa221cff85`. It already contains a browser-process Geist font path, original vector Frame mark, native left utility rail, native new-tab page and copy-link toolbar control. The September extension prototype does not describe this baseline. The development launcher no longer injects those historical extensions. The user's recording was decoded from the supplied file; its near-black Settings transition was reproduced in the source frame at 15.4 seconds.

The old build scripts could overwrite the October implementation. `scripts/chromium_overlay.py` now exports the complete native changes and resources to `native/overlay`, with a pinned revision and per-file hashes in `native/manifest.json`. Build applies that overlay after validating every file, and refuses unexpected local changes before writing. Three isolated regression tests passed: byte/index preservation and idempotence, conflict handling before writes, and resource/revision integrity. This is a durable source overlay, rather than changes existing only inside the ignored Chromium checkout.

Implemented in the real browser:

- Seeded editable native `yt`, `gh`, `wiki`, `maps` and `!` aliases once, through TemplateURLService. Existing keywords remain authoritative; deleting an alias does not re-add it on restart. The native resolver handles encoding and destination choice. Pasted `gh`, `!wiki`, `yt` and Unicode `!gh` searches passed with deterministic destination fixtures.
- Added separate working controls for empty remote suggestions and typed search suggestions under Settings > Search engine. Empty remote suggestions default off, including prefetch. The actual empty-suggestion toggle was exercised and retained across browser restart.
- Routed ordinary typing on the native NTP into the real search input without losing its first character. Editable controls, modifiers and IME composition keep their existing behaviour. Removed unavailable NTP microphone/Lens entry points from the Frame shell.
- Added persistent native rail favourites backed by a marked folder in Chromium's BookmarkModel. The native bookmark manager edits labels, URLs, ordering and removal. Normal activation reuses an exact matching tab in this window, preserving query, fragment and unsaved content; Ctrl-click explicitly permits a duplicate. Native bookmark policies and private-mode restrictions are respected. Tab-to-rail drag, configurable expanded layout and broad site reuse remain open.
- Added an explicit native Internet Archive lookup for HTTP(S) pages. The URL is submitted only on activation; the original page stays open. Error-page recovery and rich archive selection remain open.
- Added native manual sleeping of eligible background website tabs through Chromium's PageDiscardingHelper with PROACTIVE eligibility checks. Active, visible, pinned, loading, edited, audio, picture-in-picture, capture, PDF, excluded and extension-protected pages retain Chromium's protections. Ongoing downloads conservatively prevent the entire operation. No EXTERNAL force-discard path is used. A fixture verified clean background sleeping, preservation of active/dirty/protected tabs, actual native activation and reload, and retained unsaved form data. Hardware capture, active-download and audio scenarios still require behavioural coverage. Per-tab manual actions, cached thumbnails and Frame-specific automatic rules remain open.
- Enlarged native tab-search rows and secondary text, preserving open/recently closed tab functionality.
- Completed account/history/AI labels and hid unavailable OAuth-dependent sign-in/sync/AI promotions. Genuine engine credits and third-party identities remain intact.
- Aligned document and native initial backgrounds. Visual inspection also exposed light-Windows/dark-Frame media-query and card/text mismatches; the follow-up aligns WebUI's preferred colour scheme and shared surface/foreground tokens while retaining website and forced-colour behaviour.

Official Vercel Geist assets were downloaded and compared byte-for-byte with the embedded native assets; source and SIL OFL notice are preserved in `assets/fonts`. The shell renders its bundled Geist offline, while a fixture website retains Times New Roman. Font fallback, actual Windows DPI changes and all native dialog/menu combinations are not fully audited.

### Acceptance evidence

`verification/2026-10-04` contains actual isolated-browser evidence. The behavioural suite passed 13 checks, including pasted aliases, native favourites and tab reuse with unsaved input, native clipboard copy, repeated native utility activation, preference persistence, five offline internal pages, and navigation/reload. `lifecycle.json` contains the separate native sleeping/window checks and process measurements. The lifecycle test detaches remote inspection before sleeping and inspects only the diagnostic page: attaching automation to every tab would wake sleeping pages and invalidate the observation.

The native transition recording contains 750 PrintWindow captures over approximately 25 seconds, including first and repeated activation of Settings, Extensions, History, Downloads and Bookmarks. Timing and transition events are retained alongside the MP4. The sampled central content region contained no predominantly near-black frame. This sampling result does not prove that every GPU, renderer-discard, theme-change or sub-frame transition is flash-free. The recording is recreated after final palette fixes.

All test profiles are disposable directories under the workspace. `%LOCALAPPDATA%\Frame Dev\User Data` was not reset or modified for these tests. The browser uses its normal Chromium sandbox; no security-weakening flags are part of the launcher or acceptance commands.

### Remaining specification

The full supplied prompt is **not complete**. ROADMAP.md preserves every requirement ID, dependencies, acceptance criteria and remaining limitations. Outstanding workflows include the visual MRU switcher; selection conversions; tab naming/protection and explicit duplicate cleanup; named sessions; link peek; fully verified split/follower/floating views; workspaces and routing; the media/recent-files utilities; gestures; source audio boost; window coordination; and screenshot annotation. Native upstream functionality is distinguished from Frame-specific integration and tested acceptance. Windows 11, installer/update delivery and broad extension/store compatibility remain separate milestones. No overall privacy, low-RAM or speed improvement is claimed from a single fixture run.
- Added an **Archived copy** sidebar action. With an internal extension page active, it showed `OPEN A SITE FIRST`. With GitHub active, it opened a new tab at a Wayback URL and then redirected to a timestamped snapshot URL. This verifies the user-triggered lookup flow, not that every page has an archived copy or that automatic failure fallback exists.

## 5 October: native switching, split routing and measurements

Implemented the native visual recent-tab switcher with current-window MRU ordering, cached JPEG previews and favicons, closed-tab-safe weak references and native Ctrl+Tab/reverse/release/Enter/Escape handling. Its lazy Views bubble does not create a renderer or subscribe to new thumbnail captures. Accessible Previous/Next controls permit previewing without committing. The native ordering, cancellation and mouse-activation test passed without extra pages. The expanded lifecycle suite verifies that selecting and cancelling a cached sleeping preview leaves the renderer discarded, and explicit row activation wakes it. The actual sleeping screenshot was inspected. Real keyboard acceptance remains blocked by Windows refusing foreground activation; the scoped helper refuses to deliver keys to another application. The Appearance preference restores default Chromium switching when disabled.

Exposed Chromium's actual split model through the native rail and existing-tab chooser. Two real visible panes passed verification. Native UI Automation focus establishes the authoritative pane for Copy Link; both edited forms survive independent copying and closing the other pane. Renderer-only automation focus was insufficient and was corrected in the test, without speculative changes to native URL routing.

Corrected desktop verification to use `colorScheme:null`. Playwright's default light emulation was overriding native preferences and producing a false dark-scheme failure. Thirteen core checks now pass without colour emulation, and the split suite confirms navy cards (`#14263D`), readable text (`#EAF0FA`) and native dark WebUI preference. The real native Settings capture is in `verification/2026-10-05/native-settings-final.png`. Website fonts and colours retain their own native paths.

Added optional local measurement conversions to the native selected-text context menu, with working enable/metric preferences in Appearance and six significant digits. Distance, mass, explicit US/UK volume and Celsius/Fahrenheit calculations do not request a network service. Clipboard writes preserve source URLs and the existing data endpoint, respect copy restrictions, and recheck the preference and frame at execution. Sixteen real-browser checks passed, including both output systems, invalid/prose/ambiguous input, below-absolute-zero temperatures, preservation of an independent edited form and preference restart. A selection-triggered popup, currencies, time zones and clock preferences remain separate incomplete A3 requirements.

The cached official uBlock Origin Lite MV3 package also passed five isolated native checks: enabled installation, controlled advertising request interception, disabling through the real manager restores the fixture response, re-enabling restores blocking, and identity/state survive restart. This establishes the tested unpacked package only; the actual toolbar popup now also passes for its current website, after repairing Windows ExpandCollapse handling in the existing extensions button. Store delivery, MV2 and permission prompts remain open.

The native overlay now preserves 100 files. The conversion milestone rebuilt successfully in 402 steps (about five minutes); three overlay integrity/conflict regression tests still pass. Short-window acceptance found that native focus did not always scroll upper rail controls into view. A scoped native focus listener reveals focused descendants and unregisters on widget removal. Actual short-window accessibility scrolling and retained forms passed, while foreground restrictions still block real keyboard traversal. The logo remains in a fixed native header; a zero-size header regression was found and repaired before acceptance. Shared navigation selection uses muted native palette tokens instead of a hardcoded bright-blue pill. No user profile was reset.

Native tab context menus now expose **Sleep tab now** and **Never sleep this tab** through the authoritative native lifecycle, with weak tab ownership and execution-time safety rechecks. Seven lifecycle checks passed, including disabled actions for active, edited and protected pages, actual per-tab renderer discard, cached preview cancellation without waking, explicit activation/reload, retained forms and window states. A context-menu test initially matched the active document with the same title; it now filters the native TabItem role before invoking Windows UI Automation 3. No forced discard or alternate sleeping store was introduced. Per-tab never-sleep is the current native tab lifetime flag; restart/session persistence still needs work.

The configurable Frame inactivity dropdown is wired into native Memory Saver timers, with adaptive timing retained as the default. It uses the existing native domain exclusion model and proactive eligibility policy. Four actual timer checks passed: an eligible page discarded after approximately 60 seconds; edited, active and excluded pages survived; the real timer/exclusion controls and adaptive choice worked; timeout and exclusions survived restart. The initial build passed in 81 steps (1m36.66s), followed by conservative load/fullscreen/unload/download checks and retry for transient protection. The final safety build passed in two steps (15.97s). An initial restart test switched from native launch flags to Playwright defaults and triggered Chromium’s search-provider migration rollback assertion. The test now restarts with consistent native flags; no search database or user profile was reset to hide the mismatch.

Native renderer context menus now offer explicit archive lookup for the page or HTTP(S) link. The original tab remains open; an empty referrer avoids leaking unrelated page context. Five acceptance checks passed for 404/410 site content, retained forms, linked query/fragment, unsupported schemes and actual network-failure documents. The failure renderer exposes `chrome-error://chromewebdata/` to automation, while the browser retains the original HTTP(S) navigation; tests now distinguish those states. Initial browser-owned archive navigation can precede request interception and reach Wayback’s calendar redirect. Later fixture responses do not establish snapshot availability, timeout recovery or offline acceptance. A dedicated error-page button remains open. The archive source build passed in two steps (32.21s).

Native file acceptance passed five checks through the actual Windows dialogs: Netscape HTML import retains nested Unicode folders and URL query/fragment alongside existing bookmarks; malformed non-bookmark HTML preserves native data; the real export retains canonical URLs and folders; the selected fixture file reaches the website; cancelling a second chooser preserves the prior selection. Windows exposes some filename/buttons as legacy panes. The test helper follows the exact Frame owner chain, enumerates only that dialog’s standard controls, sets and verifies its filename with documented control messages, and invokes its actual Open/Save/Cancel buttons. No keyboard foreground guard was removed. The canonical Netscape fixture uses the standard line layout; a one-line fixture was not imported by the retained parser. Native bookmark WebUI Trusted Types remains enforced; exported HTML is inspected in an ordinary fixture document.

The inactivity manager now routes timeout changes through its existing injectable native delegate rather than accessing the graph from test contexts. The production rebuild passed in 18 steps (42.55s). Its fake delegate and regression translation units compiled successfully in 919 generated/compile steps (1m22s); the full `unit_tests.exe` was not built or run. Real timer acceptance is rerun after this delegate integration.


Explicit **Never sleep this tab** is now native per-tab user metadata, independent of the extension-controlled auto-discard flag. Existing native session and closed-tab extra-data paths carry the preference; private profiles have no persistent session service. Three actual session checks passed: protection survives native restart; extension-only protection is not persisted as user intent; removing a restored preference survives a second restart. Native diagnostic auto-discard changes now use the same lifecycle path as the extension API, and proactive eligibility checks consult explicit protection. This prevents direct graph updates from overriding user intent. The production policy rebuild passed in four steps (20.07s). The coordinator regression translation unit compiled in one step (13.25s); its full unit-test executable has not run.

Two actual native safety checks passed: an in-progress fixture download blocks bulk sleeping, then a clean background control discards after completion while playing audio, live synthetic camera/microphone capture, pinned/loading/active pages and a beforeunload risk remain awake. Capture uses Chromium's synthetic device with an isolated permission grant, without physical hardware; this does not verify native permission prompts or physical devices. The explicit unload fixture displays a real native Leave confirmation during cleanup. Download feedback exposed a changing accessible action name; the native button now restores its stable name after updating the feedback tooltip, rebuilt in three steps (38.44s). Hidden helper consoles and explicit UTF-8 output prevent focus and Unicode-label interference in subsequent native acceptance. A full lifecycle rerun remains pending after these harness corrections.

The overlay now retains 118 files. Reddit ordinary/bang aliases and explicit built-in quick-search restoration are being added through the existing TemplateURLService/editor. Native GUID tracking keeps added/custom/managed keyword conflicts separate from defaults and permits restoring renamed/deleted defaults only on explicit activation. Earlier deleted aliases are not silently restored during migration. This milestone is not marked verified until its production build and actual UI/restart checks finish.


Quick searches passed eleven actual native checks after the production restoration build (222 steps, 3m19.86s) and shared bare-alias resolver build (four steps, 21.67s). All five engines include ordinary and bang aliases. Actual editor add/edit/delete and restart preserve user changes; explicit Restore built-ins returns renamed/deleted defaults by GUID without removing user additions or overwriting conflicting aliases. Reddit query encoding, native bare home navigation for built-in/custom aliases, localhost port URLs and meaningful query/fragment precedence passed. The test reads real rendered native engine models rather than trying to import module paths removed by Settings bundling, and expands the native collapsed engine list before editing hidden rows. Two native keyword regression cases compiled in one step (15.64s), covering explicit query mode and localhost/URL precedence; the unit executable remains unrun. The durable overlay now contains 120 files.

Stronger capture acceptance found that Playwright's temporary permission grants are revoked when its connection closes, so that earlier run did not prove live capture protection at discard time. A fake media-permission UI also bypasses the native indicator and is unsuitable for this check. The final suite uses only synthetic devices, invokes the actual native Allow while visiting the site button in its isolated browser, and asserts capturing-video/audio protection after disconnecting automation. Both safety checks now pass with this native evidence. Cleanup removes the intentionally registered unload handler after measurements before closing the fixture. Physical hardware, PiP/fullscreen and broader multiwindow/private/race acceptance remain.
