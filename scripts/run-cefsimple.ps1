<#
  Launches cefsimple on the SECOND monitor without stealing focus, optionally
  capturing a screenshot.

  Focus discipline is deliberate and load-bearing: the primary monitor is in
  use for full-screen gaming while builds run. This script never calls
  SetForegroundWindow and never synthesizes mouse or keyboard input — the
  latter also keeps it clear of anti-cheat input hooks.

  Usage:
    powershell -ExecutionPolicy Bypass -File scripts\run-cefsimple.ps1
    ... -Url https://example.com -Screenshot out.png -CloseAfter 10
#>
param(
  [string]$Url = 'https://example.com',
  [string]$Screenshot = '',
  [int]$CloseAfter = 0   # seconds; 0 = leave it running
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class NativeWin {
  [DllImport("user32.dll")] public static extern bool SetWindowPos(
      IntPtr hWnd, IntPtr after, int X, int Y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool PrintWindow(
      IntPtr hWnd, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(
      IntPtr hWnd, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT {
      public int Left, Top, Right, Bottom; }
}
"@

$SWP_NOACTIVATE = 0x0010
$SWP_NOZORDER   = 0x0004
$PW_RENDERFULLCONTENT = 0x00000002

$repo = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repo 'third_party\cef\build\tests\cefsimple\Release\cefsimple.exe'
if (-not (Test-Path $exe)) { throw "cefsimple not built. Run scripts\build.ps1 first." }

# Prefer a non-primary screen; fall back to primary if there is only one.
$screen = [System.Windows.Forms.Screen]::AllScreens |
          Where-Object { -not $_.Primary } | Select-Object -First 1
if (-not $screen) {
  Write-Warning 'No secondary monitor found — window will open on the primary screen.'
  $screen = [System.Windows.Forms.Screen]::PrimaryScreen
}
$b = $screen.Bounds
$w = [Math]::Min(1000, $b.Width - 80)
$h = [Math]::Min(1400, $b.Height - 80)
$x = $b.X + [int](($b.Width - $w) / 2)
$y = $b.Y + [int](($b.Height - $h) / 2)

Write-Host "Launching cefsimple --url=$Url"
$p = Start-Process -FilePath $exe -ArgumentList "--url=$Url" -PassThru

$title = ''
for ($i = 0; $i -lt 60; $i++) {
  Start-Sleep -Milliseconds 500
  $p.Refresh()
  if ($p.HasExited) { throw "cefsimple exited early with code $($p.ExitCode)" }
  if ($p.MainWindowHandle -ne [IntPtr]::Zero -and $p.MainWindowTitle) {
    $title = $p.MainWindowTitle
    if ($title -ne 'cefsimple') { break }   # title became the document title
  }
}

$hwnd = $p.MainWindowHandle
if ($hwnd -ne [IntPtr]::Zero) {
  # NOACTIVATE is the whole point: move it without taking focus.
  [void][NativeWin]::SetWindowPos($hwnd, [IntPtr]::Zero, $x, $y, $w, $h,
                                  $SWP_NOACTIVATE -bor $SWP_NOZORDER)
  Write-Host "Placed on $($screen.DeviceName) at ${x},${y} (${w}x${h}) without focus"
}
Write-Host "Window title: '$title'"

if ($Screenshot) {
  Start-Sleep -Seconds 2
  $r = New-Object NativeWin+RECT
  [void][NativeWin]::GetWindowRect($hwnd, [ref]$r)
  $bmp = New-Object System.Drawing.Bitmap(($r.Right - $r.Left), ($r.Bottom - $r.Top))
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  # PrintWindow asks the window to draw itself into our DC, so it works while
  # unfocused and even partially occluded. RENDERFULLCONTENT is required for
  # GPU-composited Chromium surfaces.
  $ok = [NativeWin]::PrintWindow($hwnd, $hdc, $PW_RENDERFULLCONTENT)
  $g.ReleaseHdc($hdc)
  if (-not $ok) {
    # Fallback: copy the screen region. Still no focus change, but it captures
    # whatever is on top at those coordinates.
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
  }
  $bmp.Save($Screenshot, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  Write-Host "Screenshot: $Screenshot (PrintWindow=$ok)"
}

if ($CloseAfter -gt 0) {
  Start-Sleep -Seconds $CloseAfter
  Get-Process cefsimple -ErrorAction SilentlyContinue | Stop-Process -Force
  Write-Host 'Terminated.'
}
