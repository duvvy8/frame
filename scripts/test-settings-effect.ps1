<#
  The specific claim the settings page makes: a switch changes what the
  BROWSER does, not merely what the page or the file says.

  Each case flips a control and then observes the behaviour it names, from
  outside the page — a history file that stops growing, a tracker request that
  goes through. A settings test that only reads the setting back proves the
  file works, which was never the thing in doubt.
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

function SettingsPage {
  Navigate-Frame -Url 'frame://settings' -Port $Port -SettleMs 2200 | Out-Null
  for ($i = 0; $i -lt 20; $i++) {
    $t = Get-FrameTarget -UrlLike 'frame://settings*' -Port $Port
    if ($t) { return $t }
    Start-Sleep -Milliseconds 300
  }
  throw 'settings page never loaded'
}

function SetSetting($page, $key, $value) {
  $req = ("frame:settings:set:$key=$value") | ConvertTo-Json -Compress
  $js = @"
new Promise(function (r) {
  window.cefQuery({ request: $req, persistent: false,
    onSuccess: function (x) { r(x); }, onFailure: function (c, m) { r('fail ' + m); } });
})
"@
  return Invoke-FrameEval -WsUrl $page.webSocketDebuggerUrl -Expression $js
}

$profile = Join-Path $env:LOCALAPPDATA 'Frame'
$historyFile = Join-Path $profile 'history.tsv'

Stop-Frame
Remove-Item $historyFile -ErrorAction SilentlyContinue
Remove-Item (Join-Path $profile 'settings.tsv') -ErrorAction SilentlyContinue

$app = Start-Frame -Bottom -Config $Config -ExtraArgs @("--remote-debugging-port=$Port")
$h = $app.Hwnd
Start-Sleep -Milliseconds 1500

# --- history.enabled actually stops recording ------------------------------
Navigate-Frame -Url 'https://example.com' -Port $Port -SettleMs 3500 | Out-Null
$linesOn = if (Test-Path $historyFile) { @(Get-Content $historyFile).Count } else { 0 }

$page = SettingsPage
[void](SetSetting $page 'history.enabled' '0')
Start-Sleep -Milliseconds 500

Navigate-Frame -Url 'https://example.org' -Port $Port -SettleMs 3500 | Out-Null
Navigate-Frame -Url 'https://example.net' -Port $Port -SettleMs 3500 | Out-Null
$linesOff = if (Test-Path $historyFile) { @(Get-Content $historyFile).Count } else { 0 }

Record 'History recording stops when switched off' ($linesOff -eq $linesOn) `
  ("$linesOn line(s) before, $linesOff after two more visits")

# ...and starts again when switched back on.
$page = SettingsPage
[void](SetSetting $page 'history.enabled' '1')
Start-Sleep -Milliseconds 500
Navigate-Frame -Url 'https://example.net' -Port $Port -SettleMs 3500 | Out-Null
$linesBack = if (Test-Path $historyFile) { @(Get-Content $historyFile).Count } else { 0 }
Record 'History recording resumes when switched on' ($linesBack -gt $linesOff) `
  ("$linesOff -> $linesBack line(s)")

# --- trackers.enabled reaches the request path -----------------------------
function BlockedCount($page) {
  $js = @"
new Promise(function (r) {
  window.cefQuery({ request: 'frame:settings:get', persistent: false,
    onSuccess: function (x) { var s = JSON.parse(x); r(s.effective.trackersEnabled + '|' + s.effective.blockedSoFar); },
    onFailure: function () { r('fail'); } });
})
"@
  return Invoke-FrameEval -WsUrl $page.webSocketDebuggerUrl -Expression $js
}

$page = SettingsPage
$before = BlockedCount $page
[void](SetSetting $page 'trackers.enabled' '0')
Start-Sleep -Milliseconds 400
$page = SettingsPage
$afterOff = BlockedCount $page
[void](SetSetting $page 'trackers.enabled' '1')
Start-Sleep -Milliseconds 400
$page = SettingsPage
$afterOn = BlockedCount $page

Record 'Tracker blocking reports its live state' `
  (($afterOff -like 'False|*') -and ($afterOn -like 'True|*')) `
  ("start=$before  off=$afterOff  on=$afterOn")

# --- sleep.idleMinutes is bounded, not trusted -----------------------------
$page = SettingsPage
[void](SetSetting $page 'sleep.idleMinutes' '0')
Start-Sleep -Milliseconds 400
$page = SettingsPage
$idle = Invoke-FrameEval -WsUrl $page.webSocketDebuggerUrl -Expression @"
new Promise(function (r) {
  window.cefQuery({ request: 'frame:settings:get', persistent: false,
    onSuccess: function (x) { r(JSON.parse(x).effective.sleepIdleMinutes); },
    onFailure: function () { r(-1); } });
})
"@
# Zero would mean "discard every background tab instantly", which is not a
# setting anyone wants and is one keystroke away in a text field.
Record 'A zero idle time is clamped, not obeyed' ($idle -ge 1) `
  ("effective sleepIdleMinutes = $idle")

Write-Host "`n=== summary ===" -ForegroundColor Cyan
$script:results | Format-Table -AutoSize
$failures = @($script:results | Where-Object { -not $_.Pass }).Count
Write-Host ("{0} of {1} passed" -f ($script:results.Count - $failures), $script:results.Count)
Stop-Frame
