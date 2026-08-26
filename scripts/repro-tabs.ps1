<#
  Reproduces the reported tab-lifecycle bugs against a running Frame.

  Input is synthetic throughout: posted window messages for the mouse, and
  DevTools key dispatch for chords (see the note on Send-FrameChord for why a
  posted chord cannot carry its modifiers). Nothing here moves the physical
  cursor or foregrounds a window.

  Element positions come from the surfaces' own getBoundingClientRect rather
  than being predicted from the layout constants, so a click lands where the
  control actually is even after a CSS change.

  Usage: powershell -ExecutionPolicy Bypass -File scripts\repro-tabs.ps1
#>
param([string]$Config = 'Release', [string]$ShotDir = '', [int]$Port = 9333)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\drive.ps1"

$script:results = @()
function Record($name, $ok, $detail) {
  $script:results += [pscustomobject]@{ Test = $name; Pass = [bool]$ok; Detail = $detail }
  $tag = if ($ok) { 'PASS' } else { 'FAIL' }
  $colour = if ($ok) { 'Green' } else { 'Red' }
  Write-Host ("[{0}] {1} - {2}" -f $tag, $name, $detail) -ForegroundColor $colour
}

function Shot($h, $name) {
  if ($ShotDir) { [void](Save-FrameShot $h (Join-Path $ShotDir $name)) }
}

function TabRects { return @(Get-FrameTopbarRects '.tab:not(.is-leaving)' -Port $Port) }
function PlusRect { return (Get-FrameTopbarRects '.new-tab' -Port $Port | Select-Object -First 1) }

function Restart-ForTest {
  Stop-Frame
  $app = Start-Frame -Bottom -Config $Config -ExtraArgs @("--remote-debugging-port=$Port")
  Start-Sleep -Milliseconds 1500
  return $app
}

function New-TabByClick($h) {
  $plus = PlusRect
  if (-not $plus) { throw 'new-tab button not found' }
  Click-FrameMouse $h $plus.cx $plus.cy
  Start-Sleep -Milliseconds 900
}

# ---------------------------------------------------------------------------
$app = Restart-ForTest
$h = $app.Hwnd
Write-Host ("hwnd={0} pid={1} scale={2}" -f $h, $app.Process.Id, (Get-FrameDpiScale $h))

# ------------------------------------------------------------------ BUG 1 ---
Write-Host "`n=== BUG 1: closing one tab must not close the browser ===" -ForegroundColor Cyan
New-TabByClick $h
New-TabByClick $h
$tabs = @(TabRects)
if ($tabs.Count -ne 3) {
  Record 'Bug1 setup' $false ("expected 3 tabs, got {0}" -f $tabs.Count)
} else {
  # The close glyph sits at the right end of a tab.
  $mid = $tabs[1]
  $closeX = $mid.x + $mid.w - 12
  Write-Host ("  closing middle tab, close glyph at x={0}" -f $closeX)
  Click-FrameMouse $h $closeX $mid.cy
  Start-Sleep -Milliseconds 1400
  $alive = Test-FrameAlive $h
  if ($alive) {
    $n = @(TabRects).Count
    Record 'Bug1 close middle tab (mouse)' ($n -eq 2) ("window alive, {0} tabs left" -f $n)
  } else {
    Record 'Bug1 close middle tab (mouse)' $false 'WINDOW CLOSED - reproduced'
  }
  Shot $h 'bug1-after-close.png'
}

# Closing the ACTIVE tab is the case most likely to take the window with it.
if (-not (Test-FrameAlive $h)) { $app = Restart-ForTest; $h = $app.Hwnd }
New-TabByClick $h
$tabs = @(TabRects)
if ($tabs.Count -ge 2) {
  $active = $tabs | Where-Object { $_.cls -like '*is-active*' } | Select-Object -First 1
  if ($active) {
    Click-FrameMouse $h ($active.x + $active.w - 12) $active.cy
    Start-Sleep -Milliseconds 1400
    $alive = Test-FrameAlive $h
    Record 'Bug1 close ACTIVE tab (mouse)' $alive `
      $(if ($alive) { ("window alive, {0} tabs left" -f @(TabRects).Count) } else { 'WINDOW CLOSED - reproduced' })
  }
}

# ------------------------------------------------------------------ BUG 3 ---
Write-Host "`n=== BUG 3: Ctrl+W must close one tab, not the browser ===" -ForegroundColor Cyan
if (-not (Test-FrameAlive $h)) { $app = Restart-ForTest; $h = $app.Hwnd }
$guard = 0
while (@(TabRects).Count -lt 4 -and (Test-FrameAlive $h) -and $guard -lt 8) { New-TabByClick $h; $guard++ }

$before = @(TabRects).Count
Send-FrameChord 'W' -Ctrl -Port $Port -SettleMs 1400
$alive = Test-FrameAlive $h
if ($alive) {
  $after = @(TabRects).Count
  Record 'Bug3 Ctrl+W with 4 tabs' ($after -eq $before - 1) ("{0} -> {1} tabs, window alive" -f $before, $after)
} else {
  Record 'Bug3 Ctrl+W with 4 tabs' $false 'WINDOW CLOSED - reproduced'
}

# Repeated Ctrl+W down to (but not including) the last tab.
if (Test-FrameAlive $h) {
  $ok = $true; $detail = ''; $guard = 0
  while (@(TabRects).Count -gt 1 -and $guard -lt 12) {
    $guard++
    $n = @(TabRects).Count
    Send-FrameChord 'W' -Ctrl -Port $Port -SettleMs 900
    if (-not (Test-FrameAlive $h)) { $ok = $false; $detail = "window died going from $n tabs"; break }
    $detail = "closed down to $(@(TabRects).Count) tabs, window alive"
  }
  Record 'Bug3 repeated Ctrl+W' $ok $detail
}

# One tab left: Ctrl+W SHOULD close the window. That is correct, not a bug.
if (Test-FrameAlive $h) {
  Send-FrameChord 'W' -Ctrl -Port $Port -SettleMs 1500
  Start-Sleep -Milliseconds 800
  $gone = -not (Test-FrameAlive $h)
  Record 'Ctrl+W on last tab closes window' $gone `
    $(if ($gone) { 'window closed, as intended' } else { 'window still open with no tabs' })
  Start-Sleep -Milliseconds 900
  $crashed = $false
  try { $app.Process.Refresh(); $crashed = $app.Process.HasExited -and $app.Process.ExitCode -ne 0 } catch {}
  Record 'Last-tab close exits cleanly' (-not $crashed) `
    $(if ($crashed) { "exit code $($app.Process.ExitCode)" } else { 'exit code 0 / still shutting down' })
}

# ------------------------------------------------------------------ BUG 2 ---
Write-Host "`n=== BUG 2: New Tab must keep working ===" -ForegroundColor Cyan
$app = Restart-ForTest
$h = $app.Hwnd
$failedAt = 0
for ($i = 2; $i -le 12; $i++) {
  if (-not (Test-FrameAlive $h)) { $failedAt = $i; break }
  New-TabByClick $h
  $n = @(TabRects).Count
  if ($n -ne $i) { $failedAt = $i; Write-Host ("  click {0}: expected {0} tabs, got {1}" -f $i, $n) -ForegroundColor Yellow; break }
}
if ($failedAt) {
  Record 'Bug2 new tab, 12 clicks' $false ("stopped working at tab {0}" -f $failedAt)
} else {
  Record 'Bug2 new tab, 12 clicks' $true '12 tabs, every click created exactly one'
}
Shot $h 'bug2-twelve-tabs.png'

# Rapid clicks, no settling time - the race the slow path never hits.
if (Test-FrameAlive $h) {
  $app = Restart-ForTest; $h = $app.Hwnd
  $plus = PlusRect
  for ($i = 0; $i -lt 10; $i++) { Click-FrameMouse $h $plus.cx $plus.cy -SettleMs 40 }
  Start-Sleep -Seconds 3
  if (Test-FrameAlive $h) {
    $n = @(TabRects).Count
    Record 'Bug2 rapid new tab x10' ($n -eq 11) ("{0} tabs (want 11), window alive" -f $n)
  } else {
    Record 'Bug2 rapid new tab x10' $false 'WINDOW CLOSED'
  }
  Shot $h 'bug2-rapid.png'
}

# ------------------------------------------------------------- MIXED LOAD ---
Write-Host "`n=== Mixed Ctrl+T / Ctrl+W / Ctrl+Tab ===" -ForegroundColor Cyan
$app = Restart-ForTest
$h = $app.Hwnd
$died = $false
for ($round = 1; $round -le 6; $round++) {
  Send-FrameChord 'T' -Ctrl -Port $Port -SettleMs 350
  Send-FrameChord 'T' -Ctrl -Port $Port -SettleMs 350
  if (-not (Test-FrameAlive $h)) { $died = $true; break }
  Send-FrameChord 'TAB' -Ctrl -Port $Port -SettleMs 250
  Send-FrameChord 'W' -Ctrl -Port $Port -SettleMs 400
  if (-not (Test-FrameAlive $h)) { $died = $true; break }
}
Record 'Mixed shortcut load, 6 rounds' (-not $died) `
  $(if ($died) { ("window died in round {0}" -f $round) } else { ("survived, {0} tabs" -f @(TabRects).Count) })

Write-Host "`n=== summary ===" -ForegroundColor Cyan
$script:results | Format-Table -AutoSize
$failures = @($script:results | Where-Object { -not $_.Pass }).Count
Write-Host ("{0} of {1} passed" -f ($script:results.Count - $failures), $script:results.Count)
Stop-Frame
exit ([Math]::Min($failures, 1))
