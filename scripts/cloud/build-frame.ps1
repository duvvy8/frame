param(
    [Parameter(Mandatory=$true)][string]$ChromiumSource,
    [Parameter(Mandatory=$true)][string]$DepotTools,
    [int]$Jobs = 8
)
$ErrorActionPreference = 'Stop'
# Remote Windows builder only. Never run this entry point on the user's PC.
if ($env:FRAME_CLOUD_BUILD -ne '1') { throw 'Set FRAME_CLOUD_BUILD=1 on the remote builder only.' }
if ($Jobs -lt 1 -or $Jobs -gt 64) { throw 'Jobs must be between 1 and 64.' }
$bundleRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$sourceRoot = (Resolve-Path -LiteralPath $ChromiumSource).Path
$manifest = Get-Content -Raw -LiteralPath (Join-Path $bundleRoot 'native\manifest.json') | ConvertFrom-Json
$revision = (& git -C $sourceRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $revision -ne $manifest.revision) { throw 'Chromium revision does not match Frame.' }
foreach ($entry in $manifest.files) {
    if ($entry.path -match '(^/|^[A-Za-z]:|(^|/)\.\.(/|$))') { throw 'Unsafe overlay path' }
    $inputPath = Join-Path $bundleRoot ('native\overlay\' + $entry.path)
    $targetPath = Join-Path $sourceRoot $entry.path
    $inputHash = (Get-FileHash -LiteralPath $inputPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($inputHash -ne $entry.after) { throw ('Invalid overlay file: ' + $entry.path) }
    $targetHash = $null
    if (Test-Path -LiteralPath $targetPath) {
        $targetHash = (Get-FileHash -LiteralPath $targetPath -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    if ($targetHash -eq $entry.after) { continue }
    if ($targetHash -ne $entry.before) { throw ('Preserving unexpected source: ' + $entry.path) }
    New-Item -ItemType Directory -Force -Path (Split-Path $targetPath -Parent) | Out-Null
    Copy-Item -LiteralPath $inputPath -Destination $targetPath
}
$env:DEPOT_TOOLS_UPDATE = '0'
$env:DEPOT_TOOLS_WIN_TOOLCHAIN = '0'
$env:PATH = $DepotTools + ';' + $env:PATH
$outputRoot = Join-Path $sourceRoot 'out\FrameCloud'
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
$arguments = @'
is_debug = false
is_component_build = false
dcheck_always_on = false
symbol_level = 0
blink_symbol_level = 0
v8_symbol_level = 0
enable_widevine = true
bundle_widevine_cdm = false
proprietary_codecs = true
ffmpeg_branding = "Chrome"
'@
$argsPath = Join-Path $outputRoot 'args.gn'
if ((Test-Path -LiteralPath $argsPath) -and (Get-Content -Raw -LiteralPath $argsPath) -ne $arguments) {
    throw 'Existing cloud output uses different arguments; preserving it.'
}
if (-not (Test-Path -LiteralPath $argsPath)) {
    [System.IO.File]::WriteAllText($argsPath, $arguments, [System.Text.UTF8Encoding]::new($false))
}
Push-Location $sourceRoot
try {
    & '.\buildtools\win\gn.exe' gen 'out/FrameCloud' --fail-on-unused-args
    if ($LASTEXITCODE -ne 0) { throw 'GN generation failed' }
    & python (Join-Path $DepotTools 'autoninja.py') -C 'out/FrameCloud' -j $Jobs chrome
    if ($LASTEXITCODE -ne 0) { throw 'Cloud compilation failed' }
    $dependencies = & '.\buildtools\win\gn.exe' desc 'out/FrameCloud' '//chrome:chrome' runtime_deps
    if ($LASTEXITCODE -ne 0) { throw 'Runtime dependency query failed' }
    $packageRoot = Join-Path $sourceRoot 'FrameCloudPackage'
    if (Test-Path -LiteralPath $packageRoot) { throw 'Preserving previous cloud package' }
    New-Item -ItemType Directory -Path $packageRoot | Out-Null
    foreach ($dependency in $dependencies) {
        $relative = $dependency.Trim()
        if (-not $relative) { continue }
        if ([System.IO.Path]::IsPathRooted($relative) -or $relative -match '(^|[\\/])\.\.([\\/]|$)') {
            throw ('Unexpected runtime dependency: ' + $relative)
        }
        $origin = Join-Path $outputRoot $relative
        $destination = Join-Path $packageRoot $relative
        New-Item -ItemType Directory -Force -Path (Split-Path $destination -Parent) | Out-Null
        Copy-Item -LiteralPath $origin -Destination $destination -Recurse
    }
    Copy-Item -LiteralPath (Join-Path $bundleRoot 'native\manifest.json') -Destination (Join-Path $packageRoot 'Frame.source-manifest.json')
    Get-ChildItem -LiteralPath $packageRoot -File -Recurse | Get-FileHash -Algorithm SHA256 |
        Select-Object Path,Hash | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $packageRoot 'SHA256SUMS.json')
    Write-Output ('Cloud package ready: ' + $packageRoot)
} finally { Pop-Location }
