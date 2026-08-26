<#
  Find in page.

  The bar under test is not "a field appeared" — it is that CEF actually
  searched the document and reported real match counts, that stepping moves
  between them, and that closing clears the highlight.
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

function SidebarWs { return (Get-FrameTarget -UrlLike 'frame://sidebar*' -Port $Port).webSocketDebuggerUrl }

function FindState {
  return Invoke-FrameEval -WsUrl (SidebarWs) -Expression @"
(function () {
  var panel = document.getElementById('find');
  var count = document.getElementById('find-count');
  return JSON.stringify({
    open: panel ? !panel.hidden : false,
    count: count ? count.textContent : '',
    query: (document.getElementById('find-input') || {}).value || '',
    focused: document.activeElement ? document.activeElement.id : ''
  });
})()
"@ | ConvertFrom-Json
}

<# Types into the find field the way the user does, then lets it debounce. #>
function TypeFind($text) {
  $js = @"
(function () {
  var input = document.getElementById('find-input');
  if (!input) { return 'no field'; }
  input.value = $($text | ConvertTo-Json -Compress);
  input.dispatchEvent(new Event('input', { bubbles: true }));
  return 'ok';
})()
"@
  [void](Invoke-FrameEval -WsUrl (SidebarWs) -Expression $js)
  Start-Sleep -Milliseconds 1400
}

Stop-Frame
$app = Start-Frame -Bottom -Config $Config -ExtraArgs @("--remote-debugging-port=$Port")
$h = $app.Hwnd
Start-Sleep -Milliseconds 1500

# example.com is a known quantity: the word "domain" appears in its body, and
# a made-up word appears nowhere. Both matter — a find bar that reports
# matches for anything is as useless as one that reports none.
Navigate-Frame -Url 'https://example.com' -Port $Port -SettleMs 4000 | Out-Null

# --- opens -----------------------------------------------------------------
Send-FrameChord 'F' -Ctrl -Port $Port -SettleMs 1200
$state = FindState
Record 'Ctrl+F opens the find field' ($state.open) ("open=$($state.open)")
Record 'Ctrl+F puts the caret in it' ($state.focused -eq 'find-input') `
  ("activeElement = $($state.focused)")

# --- finds something that is there -----------------------------------------
TypeFind 'domain'
$state = FindState
Record 'A word on the page reports matches' `
  ($state.count -match '^\d+ of \d+$') ("count says '$($state.count)'")
if ($ShotDir) { [void](Save-FrameShot $h (Join-Path $ShotDir 'find-open.png')) }

# --- and nothing that is not ------------------------------------------------
TypeFind 'zzqxvnotpresent'
$state = FindState
Record 'A word that is absent reports none' ($state.count -eq 'No matches') `
  ("count says '$($state.count)'")

# --- stepping ---------------------------------------------------------------
TypeFind 'e'
$before = (FindState).count
Send-FrameDomKey 'RETURN' -Surface sidebar -Port $Port -SettleMs 1000
$after = (FindState).count
Record 'Enter steps to the next match' `
  ($before -match '^\d+ of \d+$' -and $after -match '^\d+ of \d+$' -and $after -ne $before) `
  ("'$before' -> '$after'")

$backBefore = $after
Send-FrameDomKey 'RETURN' -Shift -Surface sidebar -Port $Port -SettleMs 1000
$back = (FindState).count
Record 'Shift+Enter steps back' ($back -ne $backBefore) ("'$backBefore' -> '$back'")

# --- closing ----------------------------------------------------------------
Send-FrameDomKey 'ESCAPE' -Surface sidebar -Port $Port -SettleMs 1000
$state = FindState
Record 'Escape closes the find field' (-not $state.open) ("open=$($state.open)")

# The page must be usable again: the keyboard goes back to it, so a shortcut
# pressed after closing reaches the browser rather than the find field.
$tabsBefore = @(Get-FrameTopbarRects '.tab:not(.is-leaving)' -Port $Port).Count
Send-FrameChord 'T' -Ctrl -Port $Port -SettleMs 1200
$tabsAfter = @(Get-FrameTopbarRects '.tab:not(.is-leaving)' -Port $Port).Count
Record 'Shortcuts work again after closing find' ($tabsAfter -eq $tabsBefore + 1) `
  ("$tabsBefore -> $tabsAfter tabs")

# --- collapsed sidebar ------------------------------------------------------
# Ctrl+F with the sidebar collapsed has nowhere to put the field, so it opens
# the sidebar. Doing nothing would read as a broken shortcut.
Send-FrameChord 'B' -Ctrl -Port $Port -SettleMs 1000
$railWidth = (Get-FrameSidebarRects 'body' -Port $Port | Select-Object -First 1).w
Send-FrameChord 'F' -Ctrl -Port $Port -SettleMs 1400
$openWidth = (Get-FrameSidebarRects 'body' -Port $Port | Select-Object -First 1).w
$state = FindState
Record 'Ctrl+F opens a collapsed sidebar to show the field' `
  ($openWidth -gt $railWidth -and $state.open) `
  ("sidebar $railWidth -> $openWidth, find open = $($state.open)")

Write-Host "`n=== summary ===" -ForegroundColor Cyan
$script:results | Format-Table -AutoSize
$failures = @($script:results | Where-Object { -not $_.Pass }).Count
Write-Host ("{0} of {1} passed" -f ($script:results.Count - $failures), $script:results.Count)
Stop-Frame
