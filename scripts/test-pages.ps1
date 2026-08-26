<#
  Checks that Frame's internal pages DO something rather than merely render.

  The bar each case holds to is the same: not "the control exists", not "the
  control moved", but that the browser's own state changed as a result — and,
  where it is meant to persist, that it survived a restart.
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

function PageTarget($pageHost, [int]$TimeoutSec = 15) {
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    $t = Get-FrameTarget -UrlLike ("frame://$pageHost*") -Port $Port
    if ($t) { return $t }
    Start-Sleep -Milliseconds 300
  }
  return $null
}

function OpenInternal($h, $pageHost) {
  Navigate-Frame -Url "frame://$pageHost" -Port $Port -SettleMs 2000 | Out-Null
  return (PageTarget $pageHost)
}

function Eval($target, $js) {
  return Invoke-FrameEval -WsUrl $target.webSocketDebuggerUrl -Expression $js
}

$profile = Join-Path $env:LOCALAPPDATA 'Frame'

# A clean slate, so "the file now contains this" is a claim about this run.
Stop-Frame
foreach ($f in 'history.tsv', 'downloads.tsv', 'settings.tsv') {
  Remove-Item (Join-Path $profile $f) -ErrorAction SilentlyContinue
}

$app = Start-Frame -Bottom -Config $Config -ExtraArgs @("--remote-debugging-port=$Port")
$h = $app.Hwnd
Start-Sleep -Milliseconds 1500

# ---------------------------------------------------------------- HISTORY ---
# Visit real pages first, so history has something true to show.
Navigate-Frame -Url 'https://example.com' -Port $Port -SettleMs 3500 | Out-Null
Navigate-Frame -Url 'https://example.org' -Port $Port -SettleMs 3500 | Out-Null

$page = OpenInternal $h 'history'
if (-not $page) {
  Record 'History page loads' $false 'no frame://history target'
} else {
  Start-Sleep -Milliseconds 1200
  $rows = Eval $page "document.querySelectorAll('.row').length"
  $text = Eval $page "(document.getElementById('list').textContent || '').slice(0, 400)"
  Record 'History records real visits' ($rows -ge 2) ("$rows row(s)")
  Record 'History shows the visited hosts' `
    ($text -match 'example\.com' -and $text -match 'example\.org') `
    ($text.Substring(0, [Math]::Min(120, $text.Length)))
  if ($ShotDir) { [void](Save-FrameShot $h (Join-Path $ShotDir 'page-history.png')) }

  # Search must actually filter, not just re-render everything.
  [void](Eval $page @"
(function () {
  var s = document.getElementById('search');
  s.value = 'example.org';
  s.dispatchEvent(new Event('input', { bubbles: true }));
  return 'ok';
})()
"@)
  Start-Sleep -Milliseconds 900
  $filtered = Eval $page "document.querySelectorAll('.row').length"
  $filteredText = Eval $page "(document.getElementById('list').textContent || '')"
  Record 'History search filters' `
    ($filtered -ge 1 -and $filtered -lt $rows -and $filteredText -notmatch 'example\.com/') `
    ("$rows -> $filtered rows")

  # Removing one entry must remove it from the store, not just from the view.
  [void](Eval $page @"
(function () {
  var s = document.getElementById('search');
  s.value = '';
  s.dispatchEvent(new Event('input', { bubbles: true }));
  return 'ok';
})()
"@)
  Start-Sleep -Milliseconds 900
  [void](Eval $page "document.querySelector('.row .row-action').click()")
  Start-Sleep -Milliseconds 1200
  $afterRemove = Eval $page "document.querySelectorAll('.row').length"
  Record 'History remove deletes the entry' ($afterRemove -lt $rows) `
    ("$rows -> $afterRemove rows")

  $file = Join-Path $profile 'history.tsv'
  Record 'History is written to disk' (Test-Path $file) `
    $(if (Test-Path $file) { "$((Get-Content $file).Count) line(s) in history.tsv" } else { 'history.tsv missing' })
}

# --------------------------------------------------------------- SETTINGS ---
$page = OpenInternal $h 'settings'
if (-not $page) {
  Record 'Settings page loads' $false 'no frame://settings target'
} else {
  Start-Sleep -Milliseconds 1000
  $switches = Eval $page "document.querySelectorAll('.switch').length"
  Record 'Settings renders real controls' ($switches -ge 3) ("$switches switches")
  if ($ShotDir) { [void](Save-FrameShot $h (Join-Path $ShotDir 'page-settings.png')) }

  # The claim under test: flipping a switch changes what the BROWSER reports it
  # is doing, not merely what the page shows.
  $before = Eval $page @"
new Promise(function (r) {
  window.cefQuery({ request: 'frame:settings:get', persistent: false,
    onSuccess: function (x) { r(JSON.parse(x).effective.sleepEnabled); },
    onFailure: function () { r('fail'); } });
})
"@
  [void](Eval $page "document.querySelector('.switch').click()")
  Start-Sleep -Milliseconds 1200
  $after = Eval $page @"
new Promise(function (r) {
  window.cefQuery({ request: 'frame:settings:get', persistent: false,
    onSuccess: function (x) { r(JSON.parse(x).effective.sleepEnabled); },
    onFailure: function () { r('fail'); } });
})
"@
  Record 'A settings switch changes browser behaviour' ($before -ne $after) `
    ("effective sleepEnabled: $before -> $after")

  $file = Join-Path $profile 'settings.tsv'
  Record 'Settings are written to disk' (Test-Path $file) `
    $(if (Test-Path $file) { (Get-Content $file) -join ' ; ' } else { 'settings.tsv missing' })
}

# -------------------------------------------------------------- BOOKMARKS ---
$page = OpenInternal $h 'bookmarks'
if (-not $page) {
  Record 'Bookmarks page loads' $false 'no frame://bookmarks target'
} else {
  Start-Sleep -Milliseconds 1000
  $rows = Eval $page "document.querySelectorAll('.row').length"
  Record 'Bookmarks lists the pinned sites' ($rows -ge 1) ("$rows row(s)")
  if ($ShotDir) { [void](Save-FrameShot $h (Join-Path $ShotDir 'page-bookmarks.png')) }

  # Removing must reach the same store the sidebar draws from.
  $sidebarBefore = @(Get-FrameSidebarRects '.favorite:not(.is-empty)' -Port $Port).Count
  [void](Eval $page "document.querySelector('.row .row-action').click()")
  Start-Sleep -Milliseconds 1500
  $afterRows = Eval $page "document.querySelectorAll('.row').length"
  $sidebarAfter = @(Get-FrameSidebarRects '.favorite:not(.is-empty)' -Port $Port).Count
  Record 'Removing a favourite updates the sidebar too' `
    ($afterRows -lt $rows -and $sidebarAfter -lt $sidebarBefore) `
    ("page $rows -> $afterRows, sidebar $sidebarBefore -> $sidebarAfter")
}

# -------------------------------------------------------------- DOWNLOADS ---
$page = OpenInternal $h 'downloads'
if (-not $page) {
  Record 'Downloads page loads' $false 'no frame://downloads target'
} else {
  Start-Sleep -Milliseconds 800
  $empty = Eval $page "!!document.querySelector('.empty')"
  Record 'Downloads starts empty' $empty ''

  # A REAL download, not a simulated record. This is the case that used to be
  # impossible: Frame had no CefDownloadHandler, so a download was declined
  # outright rather than saved.
  #
  # A .zip, deliberately. Chromium DISPLAYS anything it can render - a .txt is
  # shown as a page and never reaches OnBeforeDownload - so a text file would
  # test nothing at all.
  Navigate-Frame -Url 'https://github.com/catchorg/Catch2/archive/refs/tags/v3.5.2.zip' `
    -Port $Port -SettleMs 9000 | Out-Null

  $page = OpenInternal $h 'downloads'
  Start-Sleep -Seconds 2
  $rows = Eval $page "document.querySelectorAll('.row').length"
  $state = Eval $page "(document.querySelector('.state-pill') || {}).textContent || ''"
  $name  = Eval $page "(document.querySelector('.row-title') || {}).textContent || ''"
  Record 'A real download is recorded' ($rows -ge 1) ("$rows row(s): $name [$state]")
  if ($ShotDir) { [void](Save-FrameShot $h (Join-Path $ShotDir 'page-downloads.png')) }

  $file = Join-Path $profile 'downloads.tsv'
  Record 'Downloads are written to disk' (Test-Path $file) `
    $(if (Test-Path $file) { "$((Get-Content $file).Count) line(s)" } else { 'downloads.tsv missing' })
}

# ------------------------------------------------------------- PERSISTENCE --
# The whole point of a settings file: restart and it is still true.
$sleepBefore = $after
Stop-Frame
$app = Start-Frame -Bottom -Config $Config -ExtraArgs @("--remote-debugging-port=$Port")
$h = $app.Hwnd
Start-Sleep -Milliseconds 1800

$page = OpenInternal $h 'settings'
if ($page) {
  Start-Sleep -Milliseconds 1200
  $restored = Eval $page @"
new Promise(function (r) {
  window.cefQuery({ request: 'frame:settings:get', persistent: false,
    onSuccess: function (x) { r(JSON.parse(x).effective.sleepEnabled); },
    onFailure: function () { r('fail'); } });
})
"@
  Record 'Settings survive a restart' ($restored -eq $sleepBefore) `
    ("sleepEnabled after restart: $restored (was $sleepBefore)")
}

$page = OpenInternal $h 'history'
if ($page) {
  Start-Sleep -Milliseconds 1200
  $rows = Eval $page "document.querySelectorAll('.row').length"
  Record 'History survives a restart' ($rows -ge 1) ("$rows row(s)")
}

$page = OpenInternal $h 'downloads'
if ($page) {
  Start-Sleep -Milliseconds 1200
  $rows = Eval $page "document.querySelectorAll('.row').length"
  Record 'Downloads survive a restart' ($rows -ge 1) ("$rows row(s)")
}

Write-Host "`n=== summary ===" -ForegroundColor Cyan
$script:results | Format-Table -AutoSize
$failures = @($script:results | Where-Object { -not $_.Pass }).Count
Write-Host ("{0} of {1} passed" -f ($script:results.Count - $failures), $script:results.Count)
Stop-Frame
