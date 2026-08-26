<#
  The whole suite: unit tests, then every behavioural suite in turn.

  The behavioural suites all drive a real Frame with synthetic input and read
  its state back through DevTools, so they run one at a time — each one starts
  from a fresh browser and owns the debugging port while it does.

  Usage:
    powershell -ExecutionPolicy Bypass -File scripts\test-all.ps1
    ... -SkipUnit -ShotDir out\shots
#>
param(
  [ValidateSet('Release','Debug')][string]$Config = 'Release',
  [switch]$SkipUnit,
  [string]$ShotDir = ''
)

$ErrorActionPreference = 'Continue'
$repo = Split-Path -Parent $PSScriptRoot
$summary = @()

function Run-Suite($name, $script, $extra) {
  Write-Host ""
  Write-Host ("=== {0} ===" -f $name) -ForegroundColor Cyan
  $args = @('-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot $script),
            '-Config', $Config) + $extra
  $output = & powershell @args 2>&1
  $output | ForEach-Object { Write-Host $_ }

  # The suites all end with "N of M passed", which is the one line worth
  # collecting. Parsing their own summary keeps the count in one place rather
  # than having this script re-derive it.
  $line = $output | Where-Object { $_ -match '^\d+ of \d+ passed' } | Select-Object -Last 1
  if ($line -match '^(\d+) of (\d+) passed') {
    $script:summary += [pscustomobject]@{
      Suite = $name; Passed = [int]$Matches[1]; Total = [int]$Matches[2]
    }
  } else {
    $script:summary += [pscustomobject]@{ Suite = $name; Passed = 0; Total = 0 }
  }
}

if (-not $SkipUnit) {
  Write-Host "=== unit tests ===" -ForegroundColor Cyan
  $ctest = (Get-Command ctest -ErrorAction SilentlyContinue).Source
  if (-not $ctest) {
    foreach ($p in @("$env:ProgramFiles\CMake\bin\ctest.exe",
                     "${env:ProgramFiles(x86)}\CMake\bin\ctest.exe")) {
      if (Test-Path $p) { $ctest = $p; break }
    }
  }
  $out = & $ctest --test-dir (Join-Path $repo 'build') -C $Config 2>&1
  $out | Select-Object -Last 6 | ForEach-Object { Write-Host $_ }
  # Two forms, because ctest prints the failure count only when there is one:
  #   "100% tests passed out of 50"
  #   "96% tests passed, 2 tests failed out of 50"
  $line = $out | Where-Object { $_ -match 'tests passed' } | Select-Object -Last 1
  if ($line -match '(\d+)% tests passed, (\d+) tests failed out of (\d+)') {
    $summary += [pscustomobject]@{
      Suite = 'unit (catch2)'
      Passed = [int]$Matches[3] - [int]$Matches[2]
      Total = [int]$Matches[3]
    }
  } elseif ($line -match '(\d+)% tests passed out of (\d+)') {
    $summary += [pscustomobject]@{
      Suite = 'unit (catch2)'; Passed = [int]$Matches[2]; Total = [int]$Matches[2]
    }
  } else {
    # Never silently omitted: a suite missing from the table reads as one that
    # was not run, which is exactly what this would be hiding.
    $summary += [pscustomobject]@{ Suite = 'unit (catch2)'; Passed = 0; Total = 0 }
  }
}

$shot = if ($ShotDir) { @('-ShotDir', $ShotDir) } else { @() }

Run-Suite 'tab lifecycle (the three reported bugs)' 'repro-tabs.ps1' $shot
Run-Suite 'tab context menu and sleep'             'test-menu.ps1'   $shot
Run-Suite 'internal pages'                         'test-pages.ps1'  $shot
Run-Suite 'settings change real behaviour'         'test-settings-effect.ps1' @()
Run-Suite 'keyboard commands'                      'test-shortcuts.ps1' @()
Run-Suite 'hover and interaction'                  'test-hover.ps1'  $shot
Run-Suite 'stress and resources'                   'test-stress.ps1' $shot

Write-Host ""
Write-Host "=== EVERYTHING ===" -ForegroundColor Cyan
$summary | Format-Table -AutoSize
$totalPassed = ($summary | Measure-Object Passed -Sum).Sum
$totalAll = ($summary | Measure-Object Total -Sum).Sum
Write-Host ("{0} of {1} checks passed" -f $totalPassed, $totalAll) `
  -ForegroundColor $(if ($totalPassed -eq $totalAll) { 'Green' } else { 'Red' })
exit $(if ($totalPassed -eq $totalAll) { 0 } else { 1 })
