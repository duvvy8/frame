<#
  Captures the tooltip on screen, as evidence rather than as a test.

  Moves the physical cursor, like test-real-pointer.ps1, and puts it back.
#>
param([string]$Config = 'Release', [int]$Port = 9333, [string]$ShotDir = '')

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\drive.ps1"

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Cursor2 {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out PT p);
  [StructLayout(LayoutKind.Sequential)] public struct PT { public int X, Y; }
}
"@

$origin = New-Object Cursor2+PT
[void][Cursor2]::GetCursorPos([ref]$origin)

try {
  Stop-Frame
  $app = Start-Frame -Bottom -Config $Config -ExtraArgs @("--remote-debugging-port=$Port")
  $h = $app.Hwnd
  Start-Sleep -Milliseconds 1800

  $topbarWs = (Get-FrameTarget -UrlLike 'frame://topbar*' -Port $Port).webSocketDebuggerUrl
  $r = New-Object FrameDrive+RECT
  [void][FrameDrive]::GetWindowRect($h, [ref]$r)
  $scale = Get-FrameDpiScale $h

  $rect = Get-FrameRects -WsUrl $topbarWs -Selector '#nav-reload' | Select-Object -First 1
  $x = $r.Left + [int]($rect.cx * $scale)
  $y = $r.Top + [int]($rect.cy * $scale)

  [void][Cursor2]::SetCursorPos($x, ($y + 90))
  Start-Sleep -Milliseconds 200
  [void][Cursor2]::SetCursorPos($x, $y)
  Start-Sleep -Seconds 3

  if ($ShotDir) {
    # The whole window, for context - though PrintWindow captures only the
    # window it is given, and the tooltip is a top-level window of its own.
    [void](Save-FrameShot $h (Join-Path $ShotDir 'tooltip-in-window.png'))

    # So the tooltip is captured separately, by its own handle.
    $tip = [FrameDrive]::TopLevel([uint32]$app.Process.Id) |
           Where-Object { [FrameDrive]::ClassOf($_) -eq 'FrameMenuSurface' -and
                          [FrameDrive]::IsWindowVisible($_) } |
           Select-Object -First 1
    if ($tip) {
      [void](Save-FrameShot $tip (Join-Path $ShotDir 'tooltip.png'))
      Write-Host 'captured tooltip'
    } else {
      Write-Host 'no visible tooltip window'
    }
  }
}
finally {
  [void][Cursor2]::SetCursorPos($origin.X, $origin.Y)
  Stop-Frame
}
