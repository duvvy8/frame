# Frame floating-dock redesign

## Selected visual reference

Open `floating-dock-reference.png` in this directory before implementing. The user selected this generated concept, rather than the wider sidebar concepts. It describes intended appearance, not an already implemented feature.

Keep Frame simple, subtle and recognisable. Preserve its deep navy palette, Geist Sans/Mono UI fonts and overlapping-square Frame logo.

## Appearance

- Replace the current full-height utility rail with a small vertically centred floating dock near the left edge. Use approximately 42-48 logical pixels of width, a 10-16 pixel left inset, rounded corners and a slightly lighter navy surface. Tune proportions against the reference and actual Windows scaling.
- Show actual open tabs as small favicon symbols only, not site names, tab titles or permanent captions. Use a tiny purple dot associated with the active tab. Put the new-tab plus button at the bottom of the dock.
- Keep a small Frame logo at top left, a discreet settings button at bottom left, and a split-view control near the top right.
- Hide the conventional horizontal tab strip in this layout. Retain one compact address field near the top centre, with small back, forward and reload controls beside it. Preserve native Windows caption buttons, window dragging and resizing.
- Preserve the spacious new-tab page: centred lowercase Frame wordmark, restrained search field, and small favicon-only favourite shortcuts without text labels. These shortcuts remain distinct from open tabs in the dock.
- Avoid strong borders, shadows, gradients, glass effects, oversized tiles, workspace lists, dashboards, AI panels and decorative extras. Do not impose Frame typography or colours on third-party webpages.

## Functionality

- Drive dock entries from Chromium's real tab model. Clicking activates the corresponding tab; middle-click closes it; right-click exposes appropriate native tab actions. Hover shows the tab title and domain.
- Support duplicate favicons, missing icons, loading, audio and sleeping tabs using restrained indicators and tooltips. Keep existing tab keyboard shortcuts and native lifecycle behaviour.
- Bound dock height and provide scrolling when there are many tabs. Keep the active tab reachable, including at narrow window sizes. Do not cover webpage content or intercept input outside the dock's actual hit area; reserve appropriate space or otherwise solve page overlap deliberately.
- Preserve accessible names, keyboard focus indicators, predictable traversal and usable pointer targets. Icon-only does not mean inaccessible.
- Keep the omnibox fully functional, including editable quick-search aliases and native suggestions. Preserve split browsing, session restore, tab sleep protection, favourites, extension controls, clipboard actions and other existing features. Put displaced utilities in an appropriate existing menu, rather than deleting their functionality.

## Implementation and acceptance

Read `README.md`, `BUILD_WINDOWS.md`, `ROADMAP.md` and `docs/CLOUD_DEVELOPMENT.md`. Modify the native Chromium interface, not the historical extension prototypes. Do not replace Frame with Electron, a webview, a static mockup or injected HTML chrome.

Inspect existing implementations under `native/overlay/chrome/browser/ui/views/frame/`, its layout directory, toolbar code and new-tab WebUI. Preserve the pinned revision and reproduce changes through `native/manifest.json` and the overlay workflow. Keep existing work intact and avoid historical prototype generators.

Run meaningful checks for tab activation/closure/overflow, navigation/search, narrow windows, dragging/maximise, focus, split view and lifecycle regressions. Use the actual executable and screenshots when the environment supports it. Existing verification reports are historical evidence, not proof that new changes pass. Report separately what was implemented, tested and left unverified.

Commit the completed implementation and push it to GitHub on a dedicated feature branch. Check the remote first; if the requested work already exists, verify it and avoid duplicate commits. Do not force-push or merge automatically. If publication tools are unavailable, state the exact limitation and preserve the completed patch.
