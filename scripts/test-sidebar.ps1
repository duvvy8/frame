<#
  The sidebar transition.

  What is under test is not "the sidebar moved" — it is that it ARRIVES. The
  slide resizes an off-screen browser every 8ms, and the frame for the last
  width it asks for is not always delivered: the sidebar then stays laid out at
  an intermediate one and the difference shows as a black band beside the page,
  permanently. Every check here is about the resting state that follows a
  transition, because that is where that bug lived.

  A single toggle was not enough to catch it — it reproduces on some toggles
  and not others — so the width is asserted after every one of several.
#>
param([string]$Config = 'Release', [int]$Port = 9333, [string]$ShotDir = '')

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\drive.ps1"

# The two resting widths, from src/shared/chrome_layout.h. Asked for rather
# than hardcoded twice: the surface reports the constants it was given.
$script:openWidth = 168
$script:railWidth = 8

$script:results = @()
function Record($name, $ok, $detail) {
  $script:results += [pscustomobject]@{ Test = $name; Pass = [bool]$ok; Detail = $detail }
  Write-Host ("[{0}] {1} - {2}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $name, $detail) `
    -ForegroundColor $(if ($ok) { 'Green' } else { 'Red' })
}

function SidebarWs { return (Get-FrameTarget -UrlLike 'frame://sidebar*' -Port $Port).webSocketDebuggerUrl }

<#
  The width the sidebar's DOCUMENT is laid out at.

  Deliberately not the width the browser process believes it has: that number
  was always right. innerWidth is the renderer's own answer, and the renderer
  disagreeing with the browser process is exactly the failure.
#>
function LaidOutWidth {
  return [int](Invoke-FrameEval -WsUrl (SidebarWs) -Expression 'String(innerWidth)')
}

function Toggle($hwnd, $button) {
  Click-FrameMouse $hwnd ([int]$button.cx) ([int]$button.cy)
  # Longer than kSidebarTransitionMs (210ms) plus the settle that follows it.
  Start-Sleep -Milliseconds 1200
}

Stop-Frame
$app = Start-Frame -Bottom -Config $Config -ExtraArgs @("--remote-debugging-port=$Port")
$h = $app.Hwnd
Start-Sleep -Milliseconds 1500

$button = Get-FrameTopbarRects -Selector '#sidebar-toggle' -Port $Port | Select-Object -First 1
if (-not $button) { throw 'the sidebar toggle is not in the topbar' }

Record 'The sidebar starts at its full width' `
  ((LaidOutWidth) -eq $script:openWidth) ("innerWidth = " + (LaidOutWidth))

# --- the reported bug -------------------------------------------------------
#
# Six toggles, each one asserted. Collapse and reopen are not symmetric — the
# dropped frame was seen more often on the way back out — so both directions
# are checked every time rather than once each.
$missed = @()
for ($i = 1; $i -le 6; $i++) {
  Toggle $h $button
  $want = if ($i % 2 -eq 1) { $script:railWidth } else { $script:openWidth }
  $got = LaidOutWidth
  if ($got -ne $want) { $missed += "toggle $i wanted $want, laid out at $got" }
}
Record 'Every toggle leaves the sidebar at a resting width' ($missed.Count -eq 0) `
  $(if ($missed.Count) { $missed -join '; ' } else { '6 toggles, all arrived' })

# --- toggling faster than the transition ------------------------------------
#
# A second toggle mid-slide restarts the animation from where it had reached.
# The state it settles into must still be a resting one, not wherever the
# cancelled slide happened to be.
Click-FrameMouse $h ([int]$button.cx) ([int]$button.cy)
Start-Sleep -Milliseconds 60
Click-FrameMouse $h ([int]$button.cx) ([int]$button.cy)
Start-Sleep -Milliseconds 1600
$rapid = LaidOutWidth
Record 'A toggle during the slide still settles' ($rapid -eq $script:openWidth) `
  ("innerWidth = $rapid")

# --- the page follows -------------------------------------------------------
#
# The band this suite exists for was BETWEEN the sidebar and the page, so
# neither width on its own proves it is gone. The page is a real child window,
# so its left edge is a number: the gap between the window's edge and the
# page's must be exactly what the sidebar is laid out at, with nothing over.
$sidebarNow = LaidOutWidth
$windowRect = New-Object FrameDrive+RECT
[void][FrameDrive]::GetWindowRect($h, [ref]$windowRect)
$pageLeft = $null
foreach ($child in [FrameDrive]::Children($h)) {
  $cls = New-Object System.Text.StringBuilder 256
  [void][FrameDrive]::GetClassName($child, $cls, 256)
  if ($cls.ToString() -eq 'CefBrowserWindow') {
    $childRect = New-Object FrameDrive+RECT
    [void][FrameDrive]::GetWindowRect($child, [ref]$childRect)
    $pageLeft = $childRect.Left
    break
  }
}
# The window is frameless (WM_NCCALCSIZE), so its outer left edge and its
# client origin are the same point.
$scale = Get-FrameDpiScale $h
$gap = if ($null -ne $pageLeft) { [int][Math]::Round(($pageLeft - $windowRect.Left) / $scale) } else { -1 }
Record 'The page starts where the sidebar ends' `
  ($sidebarNow -eq $script:openWidth -and $gap -eq $script:openWidth) `
  ("sidebar laid out at $sidebarNow, page begins ${gap}dip in")

# --- collapsed, then reopened by a shortcut ---------------------------------
#
# Ctrl+B is the same transition entered from the keyboard rather than the
# button, and it is the path Ctrl+L and Ctrl+F take when they have to open a
# collapsed sidebar to put a caret in it.
Send-FrameChord 'B' -Ctrl -Port $Port -SettleMs 1300
$afterCollapse = LaidOutWidth
Send-FrameChord 'B' -Ctrl -Port $Port -SettleMs 1300
$afterReopen = LaidOutWidth
Record 'Ctrl+B collapses and reopens to the resting widths' `
  ($afterCollapse -eq $script:railWidth -and $afterReopen -eq $script:openWidth) `
  ("$afterCollapse then $afterReopen")

if ($ShotDir) {
  [void](Save-FrameShot $h (Join-Path $ShotDir 'sidebar-open.png') 400)
  Send-FrameChord 'B' -Ctrl -Port $Port -SettleMs 1300
  [void](Save-FrameShot $h (Join-Path $ShotDir 'sidebar-rail.png') 400)
}

Write-Host "`n=== summary ===" -ForegroundColor Cyan
$script:results | Format-Table -AutoSize
$failures = @($script:results | Where-Object { -not $_.Pass }).Count
Write-Host ("{0} of {1} passed" -f ($script:results.Count - $failures), $script:results.Count)
Stop-Frame
