<#
  Runs Frame.

  -Safe places the window on a secondary monitor and passes --no-activate, so
  launching it cannot pull focus from a full-screen application on the primary
  display. Use it whenever something else is running fullscreen.

  Usage:
    powershell -ExecutionPolicy Bypass -File scripts\run-frame.ps1
    ... -Safe
    ... -Safe -Screenshot out.png -CloseAfter 5
#>
param(
  [switch]$Safe,
  [string]$Screenshot = '',
  [int]$CloseAfter = 0,
  [ValidateSet('Release','Debug')][string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @"
using System; using System.Runtime.InteropServices;
public class FrameNW {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

$repo = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repo "build\src\browser\$Config\frame.exe"
if (-not (Test-Path $exe)) { throw "Frame not built. Run scripts\build.ps1 first." }

$frameArgs = @()
if ($Safe) {
  $screen = [System.Windows.Forms.Screen]::AllScreens |
            Where-Object { -not $_.Primary } | Select-Object -First 1
  if (-not $screen) {
    Write-Warning 'No secondary monitor; -Safe will only suppress activation.'
    $screen = [System.Windows.Forms.Screen]::PrimaryScreen
  }
  $b = $screen.Bounds
  $w = [Math]::Min(1200, $b.Width - 80)
  $h = [Math]::Min(800, $b.Height - 80)
  $frameArgs += "--x=$($b.X + [int](($b.Width - $w)/2))"
  $frameArgs += "--y=$($b.Y + [int](($b.Height - $h)/2))"
  $frameArgs += "--width=$w", "--height=$h", '--no-activate'
}

$p = Start-Process -FilePath $exe -ArgumentList $frameArgs -PassThru
Write-Host "Frame started (pid $($p.Id))"

if ($Screenshot -or $CloseAfter -gt 0) {
  $hwnd = [IntPtr]::Zero
  for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep -Milliseconds 500
    $p.Refresh()
    if ($p.HasExited) { throw "Frame exited early with code $($p.ExitCode)" }
    if ($p.MainWindowHandle -ne [IntPtr]::Zero) { $hwnd = $p.MainWindowHandle; break }
  }

  if ($Screenshot -and $hwnd -ne [IntPtr]::Zero) {
    Start-Sleep -Seconds 3   # let the surface paint
    $r = New-Object FrameNW+RECT
    [void][FrameNW]::GetWindowRect($hwnd, [ref]$r)
    $bmp = New-Object System.Drawing.Bitmap(($r.Right-$r.Left), ($r.Bottom-$r.Top))
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    # PrintWindow works on an unfocused window; foregrounding is never needed.
    $ok = [FrameNW]::PrintWindow($hwnd, $hdc, 2)
    $g.ReleaseHdc($hdc)
    if (-not $ok) { $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size) }
    $bmp.Save($Screenshot, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Write-Host "Screenshot: $Screenshot"
  }

  if ($CloseAfter -gt 0) {
    Start-Sleep -Seconds $CloseAfter
    Get-Process frame -ErrorAction SilentlyContinue | Stop-Process -Force
    Write-Host 'Terminated.'
  }
}
