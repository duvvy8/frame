# Frame cloud build preparation

No local compilation is authorized. All local compiler/build processes were
stopped at the user's request. The cloud recipe has not been executed.

The source bundle preserves all 182 current native Frame overlay files, verified
against both the overlay manifest and the working Chromium source. It uses the
current deployed-development revision `ad04fe860307f4c408ca74ef7dbfcaaa221cff85`,
rather than substituting another browser or the older stable-candidate overlay.
Runtime acceptance is still needed before claiming all features are preserved.

Included: native overlay and manifest, cloud recipe, launcher source, first-party
extension source and this guide. Excluded: all browser profiles, cookies,
passwords, third-party extensions, credentials, local compiler outputs, and the
proprietary Widevine DLL. The local CDM package can be installed after downloading
the cloud-built browser; the cloud build enables Chromium's component CDM loader.
That avoids transferring or redistributing the locally copied CDM in CI artifacts.

## Remote Windows builder prerequisites

Chromium's documented minimum is 8 GB RAM (more than 16 GB recommended) and at
least 100 GB free NTFS space. A cold full-browser build benefits substantially
from additional CPU, memory and disk. A practical proposed machine is a Windows
x64 builder with 16 cores, 64 GB RAM and at least 500 GB SSD; this is a proposed
configuration, not a benchmark or price guarantee. Confirm provider pricing and
a spending cap before provisioning.

Use the pinned source revision's `docs/windows_build_instructions.md` for its
Visual Studio/Windows SDK requirements. Fetch Chromium using depot_tools, check
out the exact manifest revision, then run dependency synchronization and hooks.
Keep this working directory and its cache for retries. Do not use the normal
small GitHub-hosted Windows runner: its documented 14 GB SSD allocation is below
Chromium's minimum checkout/build requirement.

Extract `Frame-cloud-source.zip` on the remote machine. In a remote PowerShell
session, set `FRAME_CLOUD_BUILD=1`, then run its
`scripts/cloud/build-frame.ps1` with explicit `-ChromiumSource`, `-DepotTools`
and `-Jobs` parameters. Never run that recipe on the user's PC. It verifies source
and overlay hashes, enables Widevine plus proprietary codecs, builds the native
browser, and creates `FrameCloudPackage` from GN's declared runtime dependencies.
It does not modify source to bypass CDM verification or signing checks.

## Return and acceptance

Return the complete runtime package and SHA-256 manifest. Keep the installed
Frame release unchanged. Test in a separate folder and disposable profile first;
install the local compatible Widevine component and verify encrypted playback,
then Netflix. Preserve the existing launcher/profile/extension routes when a
verified package is promoted. A successful cloud compilation alone does not
prove Netflix compatibility, signing approval, or full feature acceptance.

## Access check

The private repository `duvvy8/frame` is accessible. At preparation time it had
zero configured self-hosted runners and zero workflows. No cloud Windows host
or cloud-provisioning connection was available in the discovered integrations.
No source was uploaded and no billable cloud job was started.

References:
- https://chromium.googlesource.com/chromium/src/+/main/docs/windows_build_instructions.md
- https://docs.github.com/en/actions/reference/runners/github-hosted-runners
