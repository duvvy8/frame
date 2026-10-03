param(
  [Parameter(Mandatory=$true)][int]$X,
  [Parameter(Mandatory=$true)][int]$Y,
  [switch]$Click
)

# Development inspection helper: Windows desktop coordinates, including negative monitors.
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class FrameInspectMouse {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extraInfo);
}
'@

if (-not [FrameInspectMouse]::SetCursorPos($X, $Y)) { throw "SetCursorPos failed" }
if ($Click) {
  Start-Sleep -Milliseconds 120
  [FrameInspectMouse]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
  [FrameInspectMouse]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
}
