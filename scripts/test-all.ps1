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
  [string]$ShotDir = '',
  # Generous: the slowest suite downloads a file and restarts the browser
  # several times. This is a deadline for a hang, not a performance budget.
  [int]$SuiteTimeoutSec = 600
)

$ErrorActionPreference = 'Continue'
$repo = Split-Path -Parent $PSScriptRoot
$summary = @()

function Run-Suite($name, $script, $extra) {
  Write-Host ""
  Write-Host ("=== {0} ===" -f $name) -ForegroundColor Cyan
  $args = @('-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot $script),
            '-Config', $Config) + $extra

  # Nothing of the previous suite may still be running. Each one owns the
  # debugging port while it runs, and a browser left behind by the last one is
  # what "No DevTools endpoint" actually means.
  Get-Process frame -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
  Start-Sleep -Milliseconds 800

  # Run in a job with a DEADLINE. A suite that hangs — waiting on a browser
  # that never came up, or on a port something else is still holding — used to
  # stall the whole run indefinitely, and an unfinished run reports nothing at
  # all. Better to lose one suite loudly than every suite silently.
  $job = Start-Job -ScriptBlock {
    param($exeArgs)
    & powershell @exeArgs 2>&1
  } -ArgumentList (, $args)

  $finished = Wait-Job $job -Timeout $SuiteTimeoutSec
  $output = Receive-Job $job -Keep
  if (-not $finished) {
    Write-Host ("!! {0} hit the {1}s deadline and was stopped" -f $name, $SuiteTimeoutSec) `
      -ForegroundColor Red
    Stop-Job $job
    # Whatever it left running would fight the next suite for the port.
    Get-Process frame -ErrorAction SilentlyContinue |
      Stop-Process -Force -ErrorAction SilentlyContinue
  }
  Remove-Job $job -Force
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
    # A suite that produced no summary DID NOT PASS — it died before it could
    # report. Recording 0 of 0 made the totals add up to a green run while
    # three suites had crashed, which is worse than a red one: it is a test
    # harness lying about coverage. 0 of 1 is the honest reading.
    $script:summary += [pscustomobject]@{ Suite = $name; Passed = 0; Total = 1 }
    Write-Host ("!! {0} produced no result — it did not finish" -f $name) `
      -ForegroundColor Red
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
    # Never silently omitted, and never 0 of 0: a suite missing from the table
    # reads as one that was not run, and 0 of 0 reads as one that passed.
    # Neither is true of a suite that failed to report.
    $summary += [pscustomobject]@{ Suite = 'unit (catch2)'; Passed = 0; Total = 1 }
  }
}

$shot = if ($ShotDir) { @('-ShotDir', $ShotDir) } else { @() }

Run-Suite 'tab lifecycle (the three reported bugs)' 'repro-tabs.ps1' $shot
Run-Suite 'tab context menu and sleep'             'test-menu.ps1'   $shot
Run-Suite 'internal pages'                         'test-pages.ps1'  $shot
Run-Suite 'settings change real behaviour'         'test-settings-effect.ps1' @()
Run-Suite 'keyboard commands'                      'test-shortcuts.ps1' @()
Run-Suite 'find in page'                           'test-find.ps1'   $shot
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
