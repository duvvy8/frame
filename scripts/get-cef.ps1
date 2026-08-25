<#
  Fetches the pinned CEF binary distribution into third_party/cef.

  The version is PINNED, not resolved to "latest", so a rebuild on another
  machine or six months from now produces the same engine. Bump deliberately;
  do not silently float.

  Usage:  powershell -ExecutionPolicy Bypass -File scripts\get-cef.ps1
#>
param(
  [switch]$Force  # re-download even if third_party/cef already exists
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# --- PIN -------------------------------------------------------------------
$CefVersion = '151.3.24+g2384915+chromium-151.0.7922.174'
$CefSha1    = '7c75fbdc8a18e8310bb64e67729974687f1f7c2b'
$Platform   = 'windows64'
# Standard distribution, not Minimal: cefsimple's sources and the CMake
# project files only ship in Standard.
# ---------------------------------------------------------------------------

$repo    = Split-Path -Parent $PSScriptRoot
$vendor  = Join-Path $repo 'third_party'
$dest    = Join-Path $vendor 'cef'
$name    = "cef_binary_${CefVersion}_${Platform}.tar.bz2"
$archive = Join-Path $vendor $name
$url     = 'https://cef-builds.spotifycdn.com/' + ($name -replace '\+','%2B')

if ((Test-Path $dest) -and -not $Force) {
  Write-Host "third_party/cef already present. Re-run with -Force to replace."
  exit 0
}

New-Item -ItemType Directory -Force -Path $vendor | Out-Null

if (-not (Test-Path $archive)) {
  Write-Host "Downloading CEF $CefVersion (~339 MB)..."
  Invoke-WebRequest -Uri $url -OutFile $archive -TimeoutSec 3600
}

Write-Host 'Verifying SHA1...'
$actual = (Get-FileHash -Path $archive -Algorithm SHA1).Hash.ToLower()
if ($actual -ne $CefSha1.ToLower()) {
  throw "SHA1 mismatch. Expected $CefSha1, got $actual. Refusing to extract."
}
Write-Host "SHA1 OK: $actual"

if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
New-Item -ItemType Directory -Force -Path $dest | Out-Null

Write-Host 'Extracting...'
& tar -xf $archive -C $dest --strip-components=1
if ($LASTEXITCODE -ne 0) { throw "tar failed with $LASTEXITCODE" }

Write-Host "CEF $CefVersion ready at third_party/cef"
