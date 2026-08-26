<#
  Fetches maintained filter lists into Frame's profile.

  Frame ships a small starter list so blocking works out of the box, but that
  list is a demonstration, not a product -- keeping up with trackers is a
  full-time job done properly by the EasyList project. This replaces the
  starter list with theirs.

  EasyPrivacy is the tracker list and is the one that matters for privacy.
  EasyList is the advertising list. Both are widely used and actively
  maintained; both are distributed under GPLv3/CC-BY-SA.

  Frame parses the host-rule subset of Adblock Plus syntax (||host^,
  $third-party, @@ exceptions) and ignores the rest, so these lists load
  correctly but only their network rules take effect. Cosmetic rules are
  skipped on purpose -- hiding an element after it has downloaded is not
  privacy.

  Usage:
    powershell -ExecutionPolicy Bypass -File scripts\get-filters.ps1
    ... -Lists easyprivacy          (trackers only, no ad blocking)
#>
param(
  [ValidateSet('both','easyprivacy','easylist')][string]$Lists = 'both',
  [switch]$KeepStarter
)

$ErrorActionPreference = 'Stop'

$sources = @{
  easyprivacy = 'https://easylist.to/easylist/easyprivacy.txt'
  easylist    = 'https://easylist.to/easylist/easylist.txt'
}

$wanted = switch ($Lists) {
  'both'        { @('easyprivacy','easylist') }
  'easyprivacy' { @('easyprivacy') }
  'easylist'    { @('easylist') }
}

$profileDir = Join-Path $env:LOCALAPPDATA 'Frame'
if (-not (Test-Path $profileDir)) { New-Item -ItemType Directory -Path $profileDir | Out-Null }
$target = Join-Path $profileDir 'filters.txt'

# Written to a temporary file and moved into place, so a failed download
# halfway through never leaves Frame with half a filter list.
$temp = Join-Path $profileDir 'filters.txt.partial'
if (Test-Path $temp) { Remove-Item $temp -Force }

$header = @(
  "! Frame filter list, assembled $(Get-Date -Format 'yyyy-MM-dd HH:mm')",
  "! by scripts\get-filters.ps1 -- do not hand-edit, it will be overwritten.",
  "!"
)
$header | Out-File -FilePath $temp -Encoding utf8

if ($KeepStarter -and (Test-Path $target)) {
  "! --- retained from the previous list ---" | Out-File -FilePath $temp -Append -Encoding utf8
  Get-Content $target | Where-Object { $_ -notmatch '^! Frame filter list' } |
    Out-File -FilePath $temp -Append -Encoding utf8
}

foreach ($name in $wanted) {
  $url = $sources[$name]
  Write-Host "Fetching $name from $url ..."
  $body = (Invoke-WebRequest -Uri $url -UseBasicParsing).Content
  "! ===== $name ($url) =====" | Out-File -FilePath $temp -Append -Encoding utf8
  $body | Out-File -FilePath $temp -Append -Encoding utf8
  $lines = ($body -split "`n").Count
  Write-Host ("  {0:N0} lines" -f $lines)
}

Move-Item -Path $temp -Destination $target -Force
$total = (Get-Content $target).Count
Write-Host ""
Write-Host "Wrote $target ($('{0:N0}' -f $total) lines)."
Write-Host "Frame loads it at startup; restart Frame to apply."
Write-Host "Only host rules take effect -- see the note at the top of this script."
