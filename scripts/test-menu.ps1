<#
  Drives the tab context menu end to end.

  The menu is its own top-level window, so mouse messages are posted to IT
  rather than to the browser window — the same synthetic path, one window
  along. Item positions come from the menu page's own layout, so a click lands
  on the row it names even after the stylesheet changes.
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

function MenuWindow($proc) {
  return ([FrameDrive]::TopLevel([uint32]$proc.Id) |
          Where-Object { [FrameDrive]::ClassOf($_) -eq 'FrameMenuSurface' } |
          Select-Object -First 1)
}

function MenuVisible($proc) {
  $m = MenuWindow $proc
  return ($m -and [FrameDrive]::IsWindowVisible($m))
}

function MenuItems {
  $t = Get-FrameTarget -UrlLike 'frame://menu*' -Port $Port
  if (-not $t) { return @() }
  return @(Get-FrameRects -WsUrl $t.webSocketDebuggerUrl -Selector '.menu-item')
}

function TabRects { return @(Get-FrameTopbarRects '.tab:not(.is-leaving)' -Port $Port) }

<# Right-clicks tab `index` and waits for the menu to be up and populated. #>
function OpenMenuOnTab($h, $proc, $index) {
  $tabs = @(TabRects)
  if ($index -ge $tabs.Count) { throw "no tab $index" }
  $t = $tabs[$index]
  Click-FrameMouse $h $t.cx $t.cy -Button Right -SettleMs 1200
  for ($i = 0; $i -lt 20; $i++) {
    if ((MenuVisible $proc) -and @(MenuItems).Count -gt 0) { return $true }
    Start-Sleep -Milliseconds 150
  }
  return $false
}

<# Clicks the menu row whose command id matches. #>
function ChooseMenuItem($proc, $command) {
  $menu = MenuWindow $proc
  if (-not $menu) { throw 'menu window missing' }
  $t = Get-FrameTarget -UrlLike 'frame://menu*' -Port $Port
  $js = "(function(){var el=document.querySelector('.menu-item[data-command=" +
        ('"' + $command + '"') + "]');if(!el)return '';var r=el.getBoundingClientRect();" +
        "return JSON.stringify({cx:r.x+r.width/2,cy:r.y+r.height/2,disabled:el.disabled});})()"
  $raw = Invoke-FrameEval -WsUrl $t.webSocketDebuggerUrl -Expression $js
  if (-not $raw) { throw "no menu item '$command'" }
  $rect = $raw | ConvertFrom-Json
  if ($rect.disabled) {
    # Dismissed rather than left standing. A menu still up when this returns
    # is one the NEXT click has to spend itself closing, which is how a caller
    # that clicks in a loop makes no progress at all.
    Send-FrameKey $h 'ESCAPE'
    Start-Sleep -Milliseconds 400
    return 'disabled'
  }
  Click-FrameMouse $menu $rect.cx $rect.cy -SettleMs 1200
  return 'clicked'
}

# ---------------------------------------------------------------------------
Stop-Frame
$app = Start-Frame -Bottom -Config $Config -ExtraArgs @("--remote-debugging-port=$Port")
$h = $app.Hwnd
$proc = $app.Process
Start-Sleep -Milliseconds 1500

# A real http page, so the sleep and favourite items are enabled rather than
# greyed out - frame:// pages are deliberately never sleepable.
Write-Host ("navigate: " + (Navigate-Frame -Url 'https://example.com' -Port $Port))

Send-FrameChord 'T' -Ctrl -Port $Port -SettleMs 1200
Write-Host ("tabs: {0}" -f @(TabRects).Count)

# --- opens -----------------------------------------------------------------
Record 'Menu opens on right-click' (OpenMenuOnTab $h $proc 0) `
  ("items: " + @(MenuItems).Count)
if ($ShotDir) {
  $m = MenuWindow $proc
  if ($m) { [void](Save-FrameShot $m (Join-Path $ShotDir 'menu.png')) }
}

# --- Escape dismisses ------------------------------------------------------
Send-FrameKey $h 'ESCAPE'
Start-Sleep -Milliseconds 700
Record 'Escape dismisses the menu' (-not (MenuVisible $proc)) ''

# --- duplicate -------------------------------------------------------------
$before = @(TabRects).Count
if (OpenMenuOnTab $h $proc 0) {
  $r = ChooseMenuItem $proc 'duplicate'
  Start-Sleep -Milliseconds 1200
  $after = @(TabRects).Count
  Record 'Duplicate tab' ($after -eq $before + 1) ("{0} -> {1} tabs ({2})" -f $before, $after, $r)
} else {
  Record 'Duplicate tab' $false 'menu did not open'
}

# --- close others ----------------------------------------------------------
if (OpenMenuOnTab $h $proc 0) {
  $r = ChooseMenuItem $proc 'close-others'
  Start-Sleep -Seconds 2
  $after = @(TabRects).Count
  Record 'Close other tabs' ($after -eq 1 -and (Test-FrameAlive $h)) `
    ("{0} tab(s) left, alive={1} ({2})" -f $after, (Test-FrameAlive $h), $r)
} else {
  Record 'Close other tabs' $false 'menu did not open'
}

# --- sleep / wake ----------------------------------------------------------
#
# Two real pages, then sleep the BACKGROUND one. The active tab is deliberately
# not sleepable — discarding the page being looked at would blank it under the
# pointer — so the menu greys that item out, and a test that right-clicked the
# active tab would only prove the item is disabled.
$plus = Get-FrameTopbarRects '.new-tab' -Port $Port | Select-Object -First 1
Click-FrameMouse $h $plus.cx $plus.cy -SettleMs 1200
Write-Host ("navigate: " + (Navigate-Frame -Url 'https://example.org' -Port $Port))

$tabs = @(Get-FrameTabs -Port $Port)
$background = $tabs | Where-Object { -not $_.active } | Select-Object -First 1
Write-Host ("tabs: {0} ({1} active)" -f $tabs.Count,
            @($tabs | Where-Object { $_.active }).Count)

if (-not $background) {
  Record 'Sleep tab' $false 'no background tab to sleep'
} else {
  # The number of WEB PAGES with a live renderer, which is the quantity sleep
  # claims to reduce.
  #
  # Counting OS processes was the first attempt and it is too blunt: Frame's
  # own surfaces come and go — the tooltip creates a renderer the first time
  # one is shown — so a page renderer going away at the same moment a chrome
  # renderer appears nets to zero and the measurement says nothing happened.
  # Live http(s) DevTools targets counts exactly the thing under test.
  function LivePages {
    return @(Get-FramePageTarget -Port $Port | Where-Object { $_.url -like 'http*' }).Count
  }

  $pagesBefore = LivePages
  $statsBefore = Get-FrameProcessStats
  Write-Host ("before sleep: {0} live page(s), {1} processes, {2} MB" -f
              $pagesBefore, $statsBefore.Count, $statsBefore.TotalMB)

  Click-FrameMouse $h $background.cx $background.cy -Button Right -SettleMs 1400
  $r = ChooseMenuItem $proc 'sleep'
  Start-Sleep -Seconds 3

  $pagesAfter = LivePages
  $statsAfter = Get-FrameProcessStats
  $asleep = @(Get-FrameTabs -Port $Port | Where-Object { $_.asleep })
  Write-Host ("after sleep:  {0} live page(s), {1} processes, {2} MB" -f
              $pagesAfter, $statsAfter.Count, $statsAfter.TotalMB)

  Record 'Sleep tab marks it asleep' ($asleep.Count -ge 1) `
    ("{0} sleeping tab(s) ({1})" -f $asleep.Count, $r)
  # The claim the feature makes, tested as a claim: the page's renderer has to
  # actually GO, not merely a class name to appear on a tab.
  Record 'Sleep releases the page renderer' ($pagesAfter -eq $pagesBefore - 1) `
    ("{0} -> {1} live page(s); {2} -> {3} MB across the process tree" -f
     $pagesBefore, $pagesAfter, $statsBefore.TotalMB, $statsAfter.TotalMB)

  if ($asleep.Count -ge 1) {
    Click-FrameMouse $h $asleep[0].cx $asleep[0].cy -SettleMs 3000
    Start-Sleep -Seconds 2
    $stillAsleep = @(Get-FrameTabs -Port $Port | Where-Object { $_.asleep })
    $woken = Get-FrameProcessStats
    Record 'Selecting a sleeping tab wakes it' ($stillAsleep.Count -eq 0) `
      ("{0} still sleeping, {1} processes" -f $stillAsleep.Count, $woken.Count)
  }

  # A sleeping tab can become the active one WITHOUT being selected: close the
  # tab beside it and it is promoted. That path does not go through SelectTab,
  # so it did not wake — leaving the browser showing an empty viewport with
  # nothing to say why.
  $tabs = @(Get-FrameTabs -Port $Port)
  $background = $tabs | Where-Object { -not $_.active } | Select-Object -First 1
  if ($background -and $tabs.Count -ge 2) {
    Click-FrameMouse $h $background.cx $background.cy -Button Right -SettleMs 1400
    [void](ChooseMenuItem $proc 'sleep')
    Start-Sleep -Seconds 3

    $sleeping = @(Get-FrameTabs -Port $Port | Where-Object { $_.asleep })
    $active = Get-FrameTabs -Port $Port | Where-Object { $_.active } | Select-Object -First 1
    if ($sleeping.Count -ge 1 -and $active) {
      # Close the ACTIVE tab, which promotes the sleeping one next to it.
      Click-FrameMouse $h $active.cx $active.cy -Button Middle -SettleMs 2500
      Start-Sleep -Seconds 3
      $nowActive = Get-FrameTabs -Port $Port | Where-Object { $_.active } | Select-Object -First 1
      Record 'A sleeping tab promoted by a close is woken' `
        ($nowActive -and -not $nowActive.asleep) `
        $(if ($nowActive) { "active tab asleep = $($nowActive.asleep)" } else { 'no active tab' })
    }
  }
}

# --- close via menu --------------------------------------------------------
#
# Two tabs first. Closing the last one closes the WINDOW, which is correct
# behaviour and not what this case is checking.
# Guarded. An unbounded loop in a test does not fail, it HANGS — and a suite
# that hangs takes the whole run with it and reports nothing at all.
$guard = 0
while (@(TabRects).Count -lt 2 -and $guard -lt 6 -and (Test-FrameAlive $h)) {
  $guard++
  $plus = Get-FrameTopbarRects '.new-tab' -Port $Port | Select-Object -First 1
  Click-FrameMouse $h $plus.cx $plus.cy -SettleMs 900
}
$before = @(TabRects).Count
if (OpenMenuOnTab $h $proc 0) {
  $r = ChooseMenuItem $proc 'close'
  Start-Sleep -Milliseconds 1500
  if (Test-FrameAlive $h) {
    $after = @(TabRects).Count
    Record 'Close tab from the menu' ($after -eq $before - 1) ("{0} -> {1} tabs" -f $before, $after)
  } else {
    Record 'Close tab from the menu' $false 'window closed'
  }
}

Write-Host "`n=== summary ===" -ForegroundColor Cyan
$script:results | Format-Table -AutoSize
$failures = @($script:results | Where-Object { -not $_.Pass }).Count
Write-Host ("{0} of {1} passed" -f ($script:results.Count - $failures), $script:results.Count)
Stop-Frame
