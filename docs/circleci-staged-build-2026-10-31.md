# CircleCI staged Frame experiment

The user authorized a Free-plan CircleCI build with no local compilation and
all current Frame features retained. CircleCI's signed-in plan page showed
`Current plan: Free` and 30,000 unused credits. No upgrade, payment method or
paid fallback was configured.

The private `cloud/widevine-staged-20261031` branch in `duvvy8/frame` contains the
verified current 182-file Frame overlay, manifest, launcher source, first-party
extensions and cloud-only scripts. The main branch and installed browser are
unchanged. Browser profiles, passwords, cookies and Widevine binaries were not
uploaded. The source snapshot matches the existing Frame development checkout.

## Staging

The finite workflow has twelve sequential Windows Medium jobs. Each job:

1. Prepares the disposable cloud image, reclaiming only allowlisted caches,
   unused .NET SDK files and old Windows SDK versions. Installs Microsoft-signed
   VS 2026 Build Tools and Windows SDK 28000 if necessary. Requires at least
   100 GiB free before restoring source/checkpoints; otherwise stops.
2. Restores prior checkpoint archives in order to the same absolute cloud path.
   Archive extraction rejects traversal and absolute paths. Previous downloaded
   archive copies are removed from that disposable job after extraction.
3. Resumes pinned public Chromium checkout/dependency sync/hooks and the
   hash-verified Frame overlay. Uses the same release-style Frame arguments plus
   Widevine loader and proprietary codec support, without a bundled CDM.
4. Allows at most twenty minutes for the owned command tree, then stops only
   that tree and checkpoints progress. This leaves part of the one-hour job
   allowance for installation, restoring, checkpointing and transfers. Whether
   these operations fit must be measured; the limit is not bypassed.
5. Stores only changed files and deletion records in a new ZIP64 archive layer.
   Existing output files, metadata and nanosecond timestamps are restored so
   incremental compilation can continue. Each checkpoint filename is unique.

Job twelve packages the complete declared runtime if compilation finished. If
not, it reports the stage limit and stops without automatic paid retries.
Credits, disk space, checkpoint upload time and per-job limits can prevent
completion. The Free plan blocks additional jobs when its credits run out;
staging does not make compute, storage or transfer consumption disappear.

## Verified so far

- All native overlay hashes match local source and the uploaded snapshot.
- The first cloud capability job passed. The Windows Medium VM had four CPU
  threads, 16 GiB RAM, a 200 GiB disk with only 65.22 GiB free, and SDKs up to
  26100. This prompted cloud-only toolchain preparation and disk reclamation.
- Local syntax checks parsed cloud PowerShell without executing it.
- Three pure-Python checks passed: checkpoint restoration preserves edits,
  deletions and timestamps; checkpoint paths cannot escape the root; cloud
  execution is refused outside CircleCI. No native compiler was invoked locally.

Pipeline 4 passed machine preparation but failed checkpointing a legitimate
Node dependency symlink. The checkpoint format now records internal file and
directory links without dereferencing them, recreates links after file restore,
and rejects targets outside the cloud state root. Five Python checks pass; the
real symlink round-trip check is skipped on this PC because Windows does not
grant symlink creation privileges. The cloud dependency tool already created
the link that exposed this failure. No successful checkpoint from pipeline 4
was available to reuse. The Free plan was rechecked before the corrected retry:
2,113 of 30,000 credits used.

The complete cloud build, returned browser, DRM module installation and Netflix
playback are not yet verified. A successful source build alone will not establish
Google signing approval or Netflix compatibility. The finished runtime must be
tested separately before replacing the installed Frame release.

Useful paths:

- `.circleci/config.yml`
- `scripts/cloud/circleci-stage.py`
- `scripts/cloud/circleci-prepare-machine.ps1`
- `scripts/cloud/build-frame.ps1`

The local staged-publish checkout is under
`verification/circleci-staged-publish-20261031`; it is isolated from the user's
working checkout. Do not execute cloud build scripts on the user's PC or
resume the stopped local compiler.
