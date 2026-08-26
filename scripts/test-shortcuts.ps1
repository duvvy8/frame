<#
  Every browser command, pressed and then checked for its effect.

  shortcuts_test.cpp already proves the chord table maps correctly. This is the
  other half: that the command the table produces reaches the browser and
  changes something. A shortcut can be perfectly mapped and still do nothing,
  which is the failure the table alone cannot see.
#>
param([string]$Config = 'Release', [int]$Port = 9333)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\drive.ps1"

$script:results = @()
function Record($name, $ok, $detail) {
  $script:results += [pscustomobject]@{ Test = $name; Pass = [bool]$ok; Detail = $detail }
  Write-Host ("[{0}] {1} - {2}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $name, $detail) `
    -ForegroundColor $(if ($ok) { 'Green' } else { 'Red' })
}

function TabCount { return @(Get-FrameTopbarRects '.tab:not(.is-leaving)' -Port $Port).Count }
function ActiveUrl {
  $t = Get-FrameTabs -Port $Port | Where-Object { $_.active } | Select-Object -First 1
  return $(if ($t) { $t.url } else { '' })
}
function Targets { return @(Get-FrameTargets -Port $Port | ForEach-Object { $_.url }) }

Stop-Frame
$app = Start-Frame -Bottom -Config $Config -ExtraArgs @("--remote-debugging-port=$Port")
$h = $app.Hwnd
Start-Sleep -Milliseconds 1800

# --- Ctrl+T ----------------------------------------------------------------
$before = TabCount
Send-FrameChord 'T' -Ctrl -Port $Port -SettleMs 1100
Record 'Ctrl+T opens a tab' ((TabCount) -eq $before + 1) ("$before -> $(TabCount)")

# --- Ctrl+Tab / Ctrl+Shift+Tab ---------------------------------------------
Send-FrameChord 'T' -Ctrl -Port $Port -SettleMs 1100
$activeBefore = (Get-FrameTabs -Port $Port | Where-Object { $_.active }).id
Send-FrameChord 'TAB' -Ctrl -Port $Port -SettleMs 800
$activeAfter = (Get-FrameTabs -Port $Port | Where-Object { $_.active }).id
Record 'Ctrl+Tab moves to another tab' ($activeAfter -ne $activeBefore) `
  ("active $activeBefore -> $activeAfter")

Send-FrameChord 'TAB' -Ctrl -Shift -Port $Port -SettleMs 800
$activeBack = (Get-FrameTabs -Port $Port | Where-Object { $_.active }).id
Record 'Ctrl+Shift+Tab moves back' ($activeBack -eq $activeBefore) `
  ("active $activeAfter -> $activeBack")

# --- Ctrl+1 / Ctrl+9 -------------------------------------------------------
$first = (Get-FrameTabs -Port $Port | Select-Object -First 1).id
Send-FrameChord '1' -Ctrl -Port $Port -SettleMs 800
Record 'Ctrl+1 selects the first tab' `
  ((Get-FrameTabs -Port $Port | Where-Object { $_.active }).id -eq $first) `
  ("active = $((Get-FrameTabs -Port $Port | Where-Object { $_.active }).id), first = $first")

$last = (@(Get-FrameTabs -Port $Port) | Select-Object -Last 1).id
Send-FrameChord '9' -Ctrl -Port $Port -SettleMs 800
Record 'Ctrl+9 selects the last tab' `
  ((Get-FrameTabs -Port $Port | Where-Object { $_.active }).id -eq $last) `
  ("active = $((Get-FrameTabs -Port $Port | Where-Object { $_.active }).id), last = $last")

# --- Ctrl+H / Ctrl+J / Ctrl+I / Ctrl+Shift+O -------------------------------
foreach ($page in @(
  @{ key = 'H'; shift = $false; host = 'history';   name = 'Ctrl+H opens history' },
  @{ key = 'J'; shift = $false; host = 'downloads'; name = 'Ctrl+J opens downloads' },
  @{ key = 'I'; shift = $false; host = 'settings';  name = 'Ctrl+I opens settings' },
  @{ key = 'O'; shift = $true;  host = 'bookmarks'; name = 'Ctrl+Shift+O opens favourites' }
)) {
  if ($page.shift) {
    Send-FrameChord $page.key -Ctrl -Shift -Port $Port -SettleMs 1800
  } else {
    Send-FrameChord $page.key -Ctrl -Port $Port -SettleMs 1800
  }
  $opened = (Targets) -match ("frame://" + $page.host)
  Record $page.name ([bool]$opened) `
    $(if ($opened) { "frame://$($page.host) is open" } else { 'page did not open' })
}

# --- Ctrl+B (sidebar) ------------------------------------------------------
$sidebarBefore = (Get-FrameSidebarRects 'body' -Port $Port | Select-Object -First 1).w
Send-FrameChord 'B' -Ctrl -Port $Port -SettleMs 900
$sidebarAfter = (Get-FrameSidebarRects 'body' -Port $Port | Select-Object -First 1).w
Record 'Ctrl+B collapses the sidebar' ($sidebarAfter -lt $sidebarBefore) `
  ("sidebar width $sidebarBefore -> $sidebarAfter")
Send-FrameChord 'B' -Ctrl -Port $Port -SettleMs 900
$sidebarBack = (Get-FrameSidebarRects 'body' -Port $Port | Select-Object -First 1).w
Record 'Ctrl+B restores the sidebar' ($sidebarBack -gt $sidebarAfter) `
  ("sidebar width $sidebarAfter -> $sidebarBack")

# --- Ctrl+L (address bar) --------------------------------------------------
# Sent to the PAGE, which is where it would be pressed while browsing, and the
# effect has to land in a different surface entirely.
Send-FrameChord 'L' -Ctrl -Port $Port -SettleMs 900
$sidebar = (Get-FrameTarget -UrlLike 'frame://sidebar*' -Port $Port).webSocketDebuggerUrl
$focused = Invoke-FrameEval -WsUrl $sidebar `
  -Expression "document.activeElement && document.activeElement.id"
Record 'Ctrl+L focuses the address field' ($focused -eq 'address') `
  ("activeElement = $focused")

# --- Ctrl+D (bookmark) -----------------------------------------------------
#
# Asserted on the LIST CONTENTS, not on the count. Favourites are deduplicated,
# so pinning a page that is already pinned correctly leaves the count where it
# was — and a count-based check then fails for the one reason that is not a
# bug, depending on what an earlier run happened to leave behind.
Navigate-Frame -Url 'https://example.net' -Port $Port -SettleMs 3500 | Out-Null
$sidebarWs = (Get-FrameTarget -UrlLike 'frame://sidebar*' -Port $Port).webSocketDebuggerUrl
function PinnedUrls {
  # dataset.url, not title: the sidebar sets title to the page's own title
  # when it has one, so reading that gives "Example Domain" rather than the
  # address the assertion is about.
  return Invoke-FrameEval -WsUrl $sidebarWs -Expression @"
Array.prototype.map.call(document.querySelectorAll('.favorite:not(.is-empty)'),
  function (el) { return el.dataset.url || el.title || ''; }).join(' | ')
"@
}
$favBefore = PinnedUrls
Send-FrameChord 'D' -Ctrl -Port $Port -SettleMs 1600
$favAfter = PinnedUrls
Record 'Ctrl+D pins the current page' ($favAfter -match 'example\.net') `
  ("favourites: $favAfter")

# --- Ctrl+R (reload) -------------------------------------------------------
# Observed through the page itself: a marker set before the reload must be gone
# after it, which only a genuine document reload achieves.
$pageTarget = Get-FramePageTarget -Port $Port |
              Where-Object { $_.url -like 'https://example.net*' } | Select-Object -First 1
if ($pageTarget) {
  [void](Invoke-FrameEval -WsUrl $pageTarget.webSocketDebuggerUrl `
    -Expression "window.__frameReloadMarker = 'set'; 'ok'")
  Send-FrameChord 'R' -Ctrl -Port $Port -SettleMs 3500
  $again = Get-FramePageTarget -Port $Port |
           Where-Object { $_.url -like 'https://example.net*' } | Select-Object -First 1
  $marker = if ($again) {
    Invoke-FrameEval -WsUrl $again.webSocketDebuggerUrl `
      -Expression "String(window.__frameReloadMarker)"
  } else { 'no target' }
  Record 'Ctrl+R reloads the document' ($marker -eq 'undefined') `
    ("marker after reload: $marker")
}

# --- Alt+Left / Alt+Right --------------------------------------------------
Navigate-Frame -Url 'https://example.com' -Port $Port -SettleMs 3500 | Out-Null
$urlBefore = ActiveUrl
Send-FrameChord 'LEFT' -Alt -Port $Port -SettleMs 3000
$urlBack = ActiveUrl
Record 'Alt+Left goes back' ($urlBack -ne $urlBefore) ("$urlBefore -> $urlBack")

Send-FrameChord 'RIGHT' -Alt -Port $Port -SettleMs 3000
$urlForward = ActiveUrl
Record 'Alt+Right goes forward' ($urlForward -eq $urlBefore) ("$urlBack -> $urlForward")

# --- Ctrl+Shift+T (reopen) -------------------------------------------------
$closedUrl = ActiveUrl
$before = TabCount
Send-FrameChord 'W' -Ctrl -Port $Port -SettleMs 1400
Send-FrameChord 'T' -Ctrl -Shift -Port $Port -SettleMs 2500
Record 'Ctrl+Shift+T reopens the closed tab' `
  ((TabCount) -eq $before -and (ActiveUrl) -like ($closedUrl.Split('?')[0] + '*')) `
  ("$before tabs, back at $(ActiveUrl)")

# --- Ctrl+N (new window) ---------------------------------------------------
$windowsBefore = @(Get-FrameWindows).Count
Send-FrameChord 'N' -Ctrl -Port $Port -SettleMs 2500
$windowsAfter = @(Get-FrameWindows).Count
Record 'Ctrl+N opens a second window' ($windowsAfter -gt $windowsBefore) `
  ("$windowsBefore -> $windowsAfter window(s)")

# Closing one window must leave the other standing. This is the multi-window
# half of the same bug that made closing a tab close the browser.
if ($windowsAfter -gt $windowsBefore) {
  $second = @(Get-FrameWindows)[-1]
  [void][FrameDrive]::PostMessage($second, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)  # WM_CLOSE
  Start-Sleep -Seconds 3
  $remaining = @(Get-FrameWindows).Count
  Record 'Closing one window leaves the other open' `
    ($remaining -eq $windowsBefore -and (Get-Process frame -ErrorAction SilentlyContinue)) `
    ("$windowsAfter -> $remaining window(s)")
}

Write-Host "`n=== summary ===" -ForegroundColor Cyan
$script:results | Format-Table -AutoSize
$failures = @($script:results | Where-Object { -not $_.Pass }).Count
Write-Host ("{0} of {1} passed" -f ($script:results.Count - $failures), $script:results.Count)
Stop-Frame
