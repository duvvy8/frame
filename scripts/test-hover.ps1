<#
  Drives the pointer across every hover-sensitive surface and checks the states
  it is meant to produce.

  This is the class of bug that reading CSS cannot find: a rule that is right
  but never matches because the element is under something else, a transition
  that never runs because the property is not animatable, a control that never
  sees a pointer because its events go somewhere else. All of it needs a
  pointer actually moving over the thing.

  The pointer here is synthetic — posted window messages — so the physical
  cursor never moves. Computed styles are read back through DevTools.
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

function Styles($ws, $selector, $props) {
  $sel = $selector | ConvertTo-Json -Compress
  $list = ($props | ForEach-Object { "'" + $_ + "'" }) -join ','
  $js = @"
(function () {
  var el = document.querySelector($sel);
  if (!el) { return ''; }
  var s = getComputedStyle(el);
  return [$list].map(function (p) { return p + '=' + s.getPropertyValue(p); }).join('; ');
})()
"@
  return Invoke-FrameEval -WsUrl $ws -Expression $js
}

<# True if the element reports a non-zero transition on any property. #>
function HasTransition($ws, $selector) {
  $sel = $selector | ConvertTo-Json -Compress
  $js = @"
(function () {
  var el = document.querySelector($sel);
  if (!el) { return 'missing'; }
  var d = getComputedStyle(el).transitionDuration || '';
  return d.split(',').some(function (v) { return parseFloat(v) > 0; }) ? 'yes' : ('no:' + d);
})()
"@
  return Invoke-FrameEval -WsUrl $ws -Expression $js
}

Stop-Frame
$app = Start-Frame -Bottom -Config $Config -ExtraArgs @("--remote-debugging-port=$Port")
$h = $app.Hwnd
Start-Sleep -Milliseconds 1800

$topbar = (Get-FrameTarget -UrlLike 'frame://topbar*' -Port $Port).webSocketDebuggerUrl
$sidebar = (Get-FrameTarget -UrlLike 'frame://sidebar*' -Port $Port).webSocketDebuggerUrl

# --- the topbar controls ---------------------------------------------------
#
# The pointer is parked away from everything first, so "before" is a genuine
# resting state rather than whatever it was last over.
Move-FrameMouse $h 600 300
Start-Sleep -Milliseconds 250

foreach ($control in @(
  @{ id = '#sidebar-toggle'; name = 'Sidebar toggle' },
  @{ id = '#nav-back';       name = 'Back button' },
  @{ id = '#nav-reload';     name = 'Reload button' },
  @{ id = '#new-tab';        name = 'New Tab button' },
  @{ id = '#win-minimize';   name = 'Minimize button' },
  @{ id = '#win-close';      name = 'Close button' }
)) {
  $rect = Get-FrameRects -WsUrl $topbar -Selector $control.id | Select-Object -First 1
  if (-not $rect) {
    Record ("{0} hover" -f $control.name) $false 'element not found'
    continue
  }
  Unhover-FrameSurface 'frame://topbar*' -Port $Port
  $before = Styles $topbar $control.id @('background-color', 'color')

  Hover-FrameSurface 'frame://topbar*' $rect.cx $rect.cy -Port $Port
  $after = Styles $topbar $control.id @('background-color', 'color')

  Record ("{0} responds to hover" -f $control.name) ($before -ne $after) `
    ("{0}  ->  {1}" -f $before, $after)
}

# --- tabs ------------------------------------------------------------------
$plus = Get-FrameTopbarRects '.new-tab' -Port $Port | Select-Object -First 1
Click-FrameMouse $h $plus.cx $plus.cy -SettleMs 1000
$tabs = @(Get-FrameVisibleTabs -Port $Port)
$idle = $tabs | Where-Object { -not $_.active } | Select-Object -First 1

if ($idle) {
  Unhover-FrameSurface 'frame://topbar*' -Port $Port
  $before = Styles $topbar '.tab:not(.is-active)' @('background-color', 'color')
  Hover-FrameSurface 'frame://topbar*' $idle.cx $idle.cy -Port $Port
  $after = Styles $topbar '.tab:not(.is-active)' @('background-color', 'color')
  Record 'An inactive tab responds to hover' ($before -ne $after) `
    ("{0}  ->  {1}" -f $before, $after)

  # The close glyph is hidden until the tab is pointed at. That IS the
  # interaction: a strip of always-visible X buttons reads as a toolbar.
  $closeOpacity = Invoke-FrameEval -WsUrl $topbar -Expression @"
(function () {
  var tab = document.querySelector('.tab:hover');
  if (!tab) { return 'no hovered tab'; }
  return getComputedStyle(tab.querySelector('.tab-close')).opacity;
})()
"@
  Record 'Hovering a tab reveals its close glyph' ($closeOpacity -eq '1') `
    ("close opacity while hovered: $closeOpacity")
  if ($ShotDir) { [void](Save-FrameShot $h (Join-Path $ShotDir 'hover-tab.png')) }

  # Leaving must put it back. A hover state that sticks is worse than none.
  Unhover-FrameSurface 'frame://topbar*' -Port $Port
  $left = Styles $topbar '.tab:not(.is-active)' @('background-color', 'color')
  Record 'Leaving a tab clears its hover state' ($left -eq $before) `
    ("{0}  ->  {1}" -f $after, $left)
}

# --- transitions exist where they are claimed ------------------------------
foreach ($claim in @(
  @{ ws = $topbar;  sel = '.tab';         name = 'Tab' },
  @{ ws = $topbar;  sel = '.tab-close';   name = 'Tab close glyph' },
  @{ ws = $topbar;  sel = '.new-tab';     name = 'New Tab button' },
  @{ ws = $topbar;  sel = '.caption-button'; name = 'Caption button' }
)) {
  $state = HasTransition $claim.ws $claim.sel
  Record ("{0} animates rather than snapping" -f $claim.name) ($state -eq 'yes') $state
}

# --- the animated properties are the cheap ones ----------------------------
#
# The 60fps claim rests on this. A transition on width or top runs layout every
# frame on the main thread; transform and opacity are composited. This checks
# what the STEADY-STATE interactions animate — the tab open/close keyframes do
# animate width, deliberately and for a quarter of a second, and are excluded.
$costly = Invoke-FrameEval -WsUrl $topbar -Expression @"
(function () {
  var bad = [];
  var expensive = ['width', 'height', 'top', 'left', 'right', 'bottom',
                   'margin', 'padding', 'font-size'];
  var seen = {};
  var all = document.querySelectorAll('.tab, .tab-close, .new-tab, .icon-button, .caption-button, .tab-dot');
  for (var i = 0; i < all.length; i++) {
    var el = all[i];
    var props = (getComputedStyle(el).transitionProperty || '').split(',');
    var durs = (getComputedStyle(el).transitionDuration || '').split(',');
    for (var j = 0; j < props.length; j++) {
      var p = props[j].trim();
      if (parseFloat(durs[j] || durs[0] || '0') <= 0) { continue; }
      if (expensive.indexOf(p) !== -1 || p === 'all') {
        var key = el.className + ':' + p;
        if (!seen[key]) { seen[key] = 1; bad.push(key); }
      }
    }
  }
  return bad.join(', ');
})()
"@
# `width` on .tab-close is deliberate and bounded: it collapses one 16px glyph
# on a crowded strip, which is not the same cost as animating a layout column.
$unexpected = ($costly -split ', ') | Where-Object { $_ -and $_ -notmatch 'tab-close:width' }
Record 'Steady-state transitions stay off layout properties' `
  (@($unexpected).Count -eq 0) `
  $(if ($unexpected) { ($unexpected -join ', ') } else { "only the deliberate one: $costly" })

# --- the sidebar -----------------------------------------------------------
$fav = Get-FrameSidebarRects '.favorite:not(.is-empty)' -Port $Port | Select-Object -First 1
if ($fav) {
  Unhover-FrameSurface 'frame://sidebar*' -Port $Port
  $before = Styles $sidebar '.favorite' @('background-color', 'transform')
  # Sidebar rects come back in WINDOW space; the surface's own pointer space
  # starts at its own origin, which is one topbar down.
  Hover-FrameSurface 'frame://sidebar*' $fav.cx ($fav.cy - 32) -Port $Port
  $after = Styles $sidebar '.favorite' @('background-color', 'transform')
  Record 'A sidebar favourite responds to hover' ($before -ne $after) `
    ("{0}  ->  {1}" -f $before, $after)
  if ($ShotDir) { [void](Save-FrameShot $h (Join-Path $ShotDir 'hover-sidebar.png')) }
}

# --- the address field -----------------------------------------------------
$addr = Get-FrameSidebarRects '#address' -Port $Port | Select-Object -First 1
if ($addr) {
  $before = Styles $sidebar '.address-input' @('color')
  Click-FrameMouse $h $addr.cx $addr.cy -SettleMs 500
  $focused = Invoke-FrameEval -WsUrl $sidebar `
    -Expression "document.activeElement && document.activeElement.id"
  Record 'Clicking the address field focuses it' ($focused -eq 'address') `
    ("activeElement = $focused")
}

# --- the menu --------------------------------------------------------------
$tabs = @(Get-FrameVisibleTabs -Port $Port)
Click-FrameMouse $h $tabs[0].cx $tabs[0].cy -Button Right -SettleMs 1500
$menuTarget = Get-FrameTarget -UrlLike 'frame://menu*' -Port $Port
$menuWin = [FrameDrive]::TopLevel([uint32]$app.Process.Id) |
           Where-Object { [FrameDrive]::ClassOf($_) -eq 'FrameMenuSurface' } |
           Select-Object -First 1

if ($menuTarget -and $menuWin) {
  $ws = $menuTarget.webSocketDebuggerUrl
  $opened = Invoke-FrameEval -WsUrl $ws `
    -Expression "document.getElementById('panel').classList.contains('is-open') ? 'open' : 'not open'"
  Record 'The menu plays its entrance' ($opened -eq 'open') $opened

  $rows = @(Get-FrameRects -WsUrl $ws -Selector '.menu-item:not(:disabled)')
  if ($rows.Count -ge 2) {
    # Posted to the MENU window, not the browser window: it is its own
    # top-level window and its own pointer target.
    Glide-FrameMouse $menuWin 10 10 $rows[1].cx $rows[1].cy -Frames 8 -DelayMs 16
    Start-Sleep -Milliseconds 300
    $focusedRows = Invoke-FrameEval -WsUrl $ws `
      -Expression "document.querySelectorAll('.menu-item.is-focused').length"
    Record 'Hovering a menu row highlights exactly one' ($focusedRows -eq 1) `
      ("$focusedRows highlighted")

    $bg = Invoke-FrameEval -WsUrl $ws -Expression @"
(function () {
  var el = document.querySelector('.menu-item.is-focused');
  return el ? getComputedStyle(el).backgroundColor : 'none';
})()
"@
    Record 'The highlighted row is actually painted' ($bg -ne 'rgba(0, 0, 0, 0)' -and $bg -ne 'none') `
      ("background: $bg")
    if ($ShotDir) { [void](Save-FrameShot $menuWin (Join-Path $ShotDir 'hover-menu.png')) }
  }
  Send-FrameKey $h 'ESCAPE'
}

Write-Host "`n=== summary ===" -ForegroundColor Cyan
$script:results | Format-Table -AutoSize
$failures = @($script:results | Where-Object { -not $_.Pass }).Count
Write-Host ("{0} of {1} passed" -f ($script:results.Count - $failures), $script:results.Count)
Stop-Frame
