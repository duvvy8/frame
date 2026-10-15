# Developing Frame from a cloud checkout

## Repository contents

Frame is a Windows native Chromium fork. This repository preserves its source changes as full postimage files in `native/overlay/`, with baseline and postimage SHA-256 hashes in `native/manifest.json`. `CHROMIUM_REVISION` pins the upstream source revision.

The full Chromium checkout, depot_tools, compiled executable, local profiles and research checkouts are deliberately ignored. They are not uploaded to GitHub. A fresh clone therefore does not include the runnable Windows build or the user's local browser profile. The selected redesign image and requirements are committed under `docs/design/` so cloud work does not depend on local Windows paths.

Start by reading `README.md`, `BUILD_WINDOWS.md`, `ROADMAP.md`, the relevant native overlay files, and `docs/design/floating-dock-brief.md`.

## Editing the overlay without a Chromium checkout

You can implement source changes directly in `native/overlay/` when `chromium/src` is unavailable. Preserve existing file bytes and newline conventions: `.gitattributes` disables text conversion for overlay files because the manifest hashes their exact bytes.

After deliberately modifying an overlay file, recalculate its manifest record's `after` field with SHA-256 of that file's actual bytes. Preserve its `before` field and the pinned upstream revision. New files have `before: null`. When adding an upstream-existing file, obtain its exact baseline at the pinned revision and hash those baseline bytes; never invent a baseline or treat an existing upstream file as new. Keep records ordered and paths repository-relative to the Chromium source root.

Do not run `scripts/chromium_overlay.py export` without a valid pinned Chromium source checkout: export discovers that checkout's changes and is not a generic manifest updater. With a populated checkout, reconcile deliberate edits and use the documented export/build flow. Never overwrite unknown local changes.

## Checks available without the Windows build

Run `python scripts/test_chromium_overlay.py`. Its three regression tests use temporary Git fixtures and do not require Chromium or Windows UI automation. Also verify every manifest postimage exists and has exactly its recorded `after` hash, that manifest paths are unique and safe, and that the revision agrees with `CHROMIUM_REVISION`.

Review source changes and build dependencies. If the environment supports fetching the pinned Chromium source, use that source for API and integration checks. Do not assume a Windows executable or Windows SDK exists in a cloud checkout. A full native build is substantially larger than the overlay repository; follow the documented requirements before attempting it.

Record unavailable native build/runtime checks honestly in the handoff. A rendered HTML approximation is not evidence that the native browser implementation works. Leave exact Windows build and acceptance steps for the existing local checkout when cloud validation cannot perform them.

## Publishing completed changes

Inspect the current branch and remote state before editing. Work on a dedicated feature branch based on the latest appropriate base. Preserve concurrent work, use normal commits, and push the completed branch to `origin` when authenticated publication is available. Verify the remote commit matches the intended local commit. Avoid duplicate commits if the desired work is already present. Do not force-push, rewrite unrelated history, or merge without a separate request.

If cloud publishing permissions are unavailable, finish the patch and report that limitation; do not claim changes were pushed. Never commit credentials, local browser profiles, build outputs or the full Chromium checkout.

## Floating-dock handoff

Run `python scripts/check_native_manifest.py` for checkout-independent integrity
validation and `python scripts/test_frame_dock_layout.py` to compile/exercise the
actual dependency-free layout calculations when a C++ compiler is installed.
These do not compile the browser or establish native visual/input acceptance.

An existing Windows source checkout with a recognised older overlay can use
`python scripts/chromium_overlay.py upgrade --from-ref b9d7949`. This reads the
previous committed manifest, checks the pinned revision and unchanged baselines,
and preflights every current file before applying new postimages. Unknown edits
refuse the whole operation. It does not export or discover Chromium changes.
See [the exact Windows upgrade/build/acceptance steps](design/floating-dock-verification.md).
