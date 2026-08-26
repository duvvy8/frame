<#
  Stress and resource behaviour.

  Everything here is deliberately harder than ordinary use: no settling time
  between actions, tabs closed while they are still loading, and enough churn
  to expose a leak that a handful of tabs would hide.

  Memory is reported as the whole process tree — Frame is a browser, so a
  "leak" that shows up only in the browser process while renderers pile up is
  not the number worth watching.
#>
param([string]$Config = 'Release', [int]$Port = 9333, [string]$ShotDir = '')

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\drive.ps1"

$script:results = @()
function Record($name, $ok, $detail) {
  $script:results += [pscustomobject]@{ Test = $name; Pass = [bool]$ok; Detail = $detail }
  Write-Host ("[{0}] {1} - {2}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $name, $detail) `
    -ForegroundColor $(if ($ok) { 'Green' } else { 'Red' })
}

function TabCount { return @(Get-FrameTopbarRects '.tab:not(.is-leaving)' -Port $Port).Count }
function Plus { return (Get-FrameTopbarRects '.new-tab' -Port $Port | Select-Object -First 1) }

Stop-Frame
$app = Start-Frame -Bottom -Config $Config -ExtraArgs @("--remote-debugging-port=$Port")
$h = $app.Hwnd
$proc = $app.Process
Start-Sleep -Milliseconds 1800

$baseline = Get-FrameProcessStats
Write-Host ("baseline: {0} processes, {1} MB" -f $baseline.Count, $baseline.TotalMB)

# --- 20 tabs ---------------------------------------------------------------
$plus = Plus
for ($i = 0; $i -lt 19; $i++) {
  if (-not (Test-FrameAlive $h)) { break }
  Click-FrameMouse $h $plus.cx $plus.cy -SettleMs 200
  # The + button does not move — that is the whole point of pinning it outside
  # the scroller — but re-reading it proves that rather than assuming it.
  $plus = Plus
}
Start-Sleep -Seconds 3
$twenty = TabCount
$peak = Get-FrameProcessStats
Record 'Twenty tabs open without incident' ($twenty -eq 20 -and (Test-FrameAlive $h)) `
  ("{0} tabs, {1} processes, {2} MB" -f $twenty, $peak.Count, $peak.TotalMB)
if ($ShotDir) { [void](Save-FrameShot $h (Join-Path $ShotDir 'stress-20-tabs.png')) }

# The + button must still be inside the window with twenty tabs open. This is
# the exact failure that made New Tab look broken.
$r = New-Object FrameDrive+RECT
[void][FrameDrive]::GetClientRect($h, [ref]$r)
$clientDip = $r.Right / (Get-FrameDpiScale $h)
$plus = Plus
Record 'New Tab stays reachable at twenty tabs' `
  ($plus -and $plus.cx -gt 0 -and $plus.cx -lt $clientDip) `
  ("+ centre at x={0}, client width {1}" -f [Math]::Round($plus.cx), [Math]::Round($clientDip))

# The tab you just opened has to be the one you can see.
#
# It was not: a tab enters by growing from zero width, so at the moment it is
# inserted the strip has nothing to scroll for, and on a full strip the new
# tab — which is the active one — ended up scrolled off the right-hand end
# showing a two-pixel sliver of its own accent border.
$visible = @(Get-FrameVisibleTabs -Port $Port)
$activeVisible = $visible | Where-Object { $_.active } | Select-Object -First 1
Record 'The newly opened tab is fully in view' ([bool]$activeVisible) `
  $(if ($activeVisible) {
      "active tab at x={0}, width {1}" -f [Math]::Round($activeVisible.x), [Math]::Round($activeVisible.w)
    } else { 'the active tab is scrolled out of the strip' })

# --- rapid close -----------------------------------------------------------
#
# Middle-click, which closes without needing to hit a 16px glyph. Aimed at a
# VISIBLE tab: with twenty open the strip is scrolled, and a tab scrolled out
# of it still reports a rectangle — one that is over the nav cluster, where
# clicking closes nothing.
$before = TabCount
$closed = 0
for ($i = 0; $i -lt 15; $i++) {
  if (-not (Test-FrameAlive $h)) { break }
  if ((TabCount) -le 1) { break }
  $tabs = @(Get-FrameVisibleTabs -Port $Port)
  if (-not $tabs) { break }
  Click-FrameMouse $h $tabs[0].cx $tabs[0].cy -Button Middle -SettleMs 90
  $closed++
}
Start-Sleep -Seconds 3
$left = if (Test-FrameAlive $h) { TabCount } else { -1 }
Record 'Rapid closing does not take the window with it' (Test-FrameAlive $h) `
  ("{0} close(s), {1} -> {2} tabs" -f $closed, $before, $left)

# A click every 90ms is faster than the 180ms a tab takes to collapse out of
# the strip, so a click can land on a tab that has already gone and on nothing
# that replaced it yet. That is the harness racing the animation, not the
# browser dropping a request — and it is not distinguishable from outside at
# that speed, so it is not asserted on here.
#
# What IS asserted: at a cadence a person could produce, not one close is lost.
$before = TabCount
$closed = 0
for ($i = 0; $i -lt 5; $i++) {
  if ((TabCount) -le 1) { break }
  $tabs = @(Get-FrameVisibleTabs -Port $Port)
  if (-not $tabs) { break }
  Click-FrameMouse $h $tabs[0].cx $tabs[0].cy -Button Middle -SettleMs 320
  $closed++
}
Start-Sleep -Seconds 2
$left = TabCount
Record 'No close is lost at a human cadence' ($left -eq $before - $closed) `
  ("{0} close(s) removed {1} tab(s)" -f $closed, ($before - $left))

# --- churn: open and close repeatedly --------------------------------------
# The shape a leak shows up in. Each cycle opens three tabs and closes them,
# so anything retained per tab accumulates thirty times over.
if (Test-FrameAlive $h) {
  $settled = Get-FrameProcessStats
  Write-Host ("before churn: {0} processes, {1} MB" -f $settled.Count, $settled.TotalMB)

  for ($cycle = 0; $cycle -lt 10; $cycle++) {
    if (-not (Test-FrameAlive $h)) { break }
    $plus = Plus
    for ($i = 0; $i -lt 3; $i++) { Click-FrameMouse $h $plus.cx $plus.cy -SettleMs 150 }
    Start-Sleep -Milliseconds 400
    for ($i = 0; $i -lt 3; $i++) {
      if ((TabCount) -le 1) { break }
      $tabs = @(Get-FrameVisibleTabs -Port $Port)
      if (-not $tabs) { break }
      Click-FrameMouse $h $tabs[0].cx $tabs[0].cy -Button Middle -SettleMs 150
    }
    Start-Sleep -Milliseconds 300
  }
  Start-Sleep -Seconds 4
  $after = Get-FrameProcessStats
  Write-Host ("after churn:  {0} processes, {1} MB" -f $after.Count, $after.TotalMB)

  Record 'Survives thirty open/close cycles' (Test-FrameAlive $h) `
    ("{0} tabs left" -f $(if (Test-FrameAlive $h) { TabCount } else { 'n/a' }))

  # Processes are the honest measure: one per live renderer. If closing tabs
  # leaked renderers, this is where thirty of them would be sitting.
  Record 'No renderer processes left behind' ($after.Count -le $settled.Count) `
    ("{0} -> {1} processes" -f $settled.Count, $after.Count)

  # Memory is allowed to grow — caches, JIT, the allocator not returning pages
  # — but not without bound. A 2x growth over a settled baseline across thirty
  # tab lifetimes would be a real leak rather than ordinary drift.
  $ratio = if ($settled.TotalMB -gt 0) { $after.TotalMB / $settled.TotalMB } else { 1 }
  Record 'Memory does not grow without bound' ($ratio -lt 2.0) `
    ("{0} -> {1} MB (x{2})" -f $settled.TotalMB, $after.TotalMB, [Math]::Round($ratio, 2))
}

# --- keyboard churn --------------------------------------------------------
if (Test-FrameAlive $h) {
  $died = $false
  for ($i = 0; $i -lt 12; $i++) {
    Send-FrameChord 'T' -Ctrl -Port $Port -SettleMs 200
    if (-not (Test-FrameAlive $h)) { $died = $true; break }
  }
  for ($i = 0; $i -lt 6; $i++) {
    Send-FrameChord 'TAB' -Ctrl -Port $Port -SettleMs 120
  }
  for ($i = 0; $i -lt 10; $i++) {
    if (@(Get-FrameTopbarRects '.tab:not(.is-leaving)' -Port $Port).Count -le 1) { break }
    Send-FrameChord 'W' -Ctrl -Port $Port -SettleMs 250
    if (-not (Test-FrameAlive $h)) { $died = $true; break }
  }
  Record 'Keyboard churn leaves the browser standing' (-not $died) `
    ("{0} tabs" -f $(if (Test-FrameAlive $h) { TabCount } else { 'n/a' }))
}

# --- menu churn ------------------------------------------------------------
if (Test-FrameAlive $h) {
  $tabs = @(Get-FrameVisibleTabs -Port $Port)
  $menuFails = 0
  for ($i = 0; $i -lt 10; $i++) {
    Click-FrameMouse $h $tabs[0].cx $tabs[0].cy -Button Right -SettleMs 350
    Send-FrameKey $h 'ESCAPE'
    Start-Sleep -Milliseconds 250
    $m = [FrameDrive]::TopLevel([uint32]$proc.Id) |
         Where-Object { [FrameDrive]::ClassOf($_) -eq 'FrameMenuSurface' }
    if (@($m).Count -ne 1) { $menuFails++ }
  }
  $stats = Get-FrameProcessStats
  Record 'Ten menu open/close cycles reuse one surface' ($menuFails -eq 0) `
    ("{0} cycle(s) with the wrong window count; {1} processes" -f $menuFails, $stats.Count)
}

Write-Host "`n=== summary ===" -ForegroundColor Cyan
$script:results | Format-Table -AutoSize
$failures = @($script:results | Where-Object { -not $_.Pass }).Count
Write-Host ("{0} of {1} passed" -f ($script:results.Count - $failures), $script:results.Count)
Stop-Frame
