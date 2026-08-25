<#
  Configures and builds Frame, plus the cefsimple toolchain-proof sample.

  Replaces the Electron build's `npm start` / `npm test` / `npm run smoke`.
  No Node, no npm — that is the point of the rewrite.

  Usage:
    powershell -ExecutionPolicy Bypass -File scripts\build.ps1
    ... -Config Debug -SkipCef -SkipTests
#>
param(
  [ValidateSet('Release','Debug')][string]$Config = 'Release',
  [switch]$SkipCef,     # skip the cefsimple sample
  [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Resolve-CMake {
  $c = Get-Command cmake -ErrorAction SilentlyContinue
  if ($c) { return $c.Source }
  # CMake is often installed after the current shell captured its PATH.
  foreach ($p in @(
      "$env:ProgramFiles\CMake\bin\cmake.exe",
      "${env:ProgramFiles(x86)}\CMake\bin\cmake.exe")) {
    if (Test-Path $p) { return $p }
  }
  throw 'cmake not found. Install it (winget install Kitware.CMake) or open a new shell.'
}

$cmake = Resolve-CMake
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
$gen = @('-G', 'Visual Studio 17 2022', '-A', 'x64')

if (-not $SkipCef) {
  $cefRoot = Join-Path $repo 'third_party\cef'
  if (-not (Test-Path $cefRoot)) {
    throw 'third_party/cef missing. Run scripts\get-cef.ps1 first.'
  }
  Write-Host '=== Building cefsimple (toolchain proof) ==='
  & $cmake -S $cefRoot -B (Join-Path $cefRoot 'build') @gen
  if ($LASTEXITCODE) { throw "CEF configure failed ($LASTEXITCODE)" }
  & $cmake --build (Join-Path $cefRoot 'build') --config $Config --target cefsimple --parallel
  if ($LASTEXITCODE) { throw "cefsimple build failed ($LASTEXITCODE)" }
}

Write-Host '=== Building Frame ==='
$build = Join-Path $repo 'build'
& $cmake -S $repo -B $build @gen
if ($LASTEXITCODE) { throw "Frame configure failed ($LASTEXITCODE)" }
& $cmake --build $build --config $Config --parallel
if ($LASTEXITCODE) { throw "Frame build failed ($LASTEXITCODE)" }

if (-not $SkipTests) {
  Write-Host '=== Tests ==='
  & $ctest --test-dir $build -C $Config --output-on-failure
  if ($LASTEXITCODE) { throw "Tests failed ($LASTEXITCODE)" }
}

Write-Host 'OK.'
