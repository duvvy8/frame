# Frame roadmap

This is Frame's plan, informed by the [Ember README](https://github.com/v11exe/ember/blob/main/README.md) and [Ember roadmap](https://github.com/v11exe/ember/blob/main/ROADMAP.md). Ember's completed status does **not** imply the feature exists in Frame. Frame uses full Chromium, so each idea needs a separate design and compatibility check in that codebase.

## Milestone 1 — browser foundation (current)

- Pin and fetch full Chromium; build and launch a clean baseline.
- Build Frame with its own development name, icon, and profile; keep Chromium's existing omnibox, navigation, tabs, Blink/V8 rendering, and extension architecture.
- Add a restrained dark blue/purple first pass. Use Geist for primary controls, IBM Plex Sans selectively, and IBM Plex Mono for technical readings. The current source work only supplies those fonts in a new-tab page; native toolbar font integration remains open.
- Live-check `chrome://extensions` and, if feasible, uBlock Origin Lite on a test page.
- Record actual verification in `PROGRESS.md`.

## Milestone 2 — native window interaction and polish

Manually verify the open Frame window's address bar URL entry and search, back/forward/reload buttons, new/close tab controls, and restart with the normal launcher profile. Integrate Geist into appropriate native controls without disrupting Chromium's accessibility and layout. Fix any usability defects found. This is the **single next milestone**; the other features remain later candidates.

## Later candidates, after a working build

1. Favourites/instant site shortcuts, with persistent ordering and tab reuse; keep ordinary bookmarks intact. An initial side panel version exists; refine its placement, controls, and interaction after a native-window check.
2. Configurable quick searches/bangs integrated with Chromium's omnibox, without mistaking URLs for search aliases. The initial side panel field has fixed `yt`, `gh`, `wiki`, and `maps` aliases; omnibox integration is still open.
3. Tab hibernation with explicit protection for active, media, capture, downloads, and unsaved forms; measure real memory effects before making claims.
4. A visual MRU Ctrl+Tab switcher, including sleeping-tab previews and keyboard cancel/commit behaviour.
5. Selection conversions for units, currencies, temperatures, and times, with clear data sources and user preferences.
6. A user-triggered Internet Archive fallback for failures and dead links; avoid automatic redirects. The first sidebar button now opens a Wayback lookup for the current page. Automatic failure detection and a richer fallback flow remain open.
7. Extension compatibility work: standard Manifest V3 extensions first, Chrome Web Store installation as a separate check, classic uBlock Origin/Manifest V2 as a later investigation.
8. Accessibility, Windows integration, update strategy, packaging/installer, privacy behaviour, and performance measurements as separate milestones with explicit tests.

No logo is planned for milestone 1. The icon in `assets/frame-dev.ico` is temporary and says DEV.
