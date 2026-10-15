param([Parameter(Mandatory=$true)][long]$WindowHandle,
      [Parameter(Mandatory=$true)][ValidateSet('CtrlDown','CtrlUp','ShiftDown','ShiftUp','Tab','Escape','Enter')][string]$Key,
      [switch]$Activate)
# Real native key delivery for the disposable Frame test window only.
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class FrameTestKey {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint first, uint second, bool attach);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
  [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
}
'@
[uint32]$frameOwnerId = 0
[void][FrameTestKey]::GetWindowThreadProcessId([IntPtr]$WindowHandle,[ref]$frameOwnerId)
if ((Get-Process -Id $frameOwnerId).ProcessName -ne 'Frame') { throw 'Expected isolated Frame window' }
if ($Activate) {
  [uint32]$framePreviousId = 0
  $framePreviousThread = [FrameTestKey]::GetWindowThreadProcessId([FrameTestKey]::GetForegroundWindow(),[ref]$framePreviousId)
  $frameTestThread = [FrameTestKey]::GetCurrentThreadId()
  $frameAttached = [FrameTestKey]::AttachThreadInput($frameTestThread,$framePreviousThread,$true)
  try { [void][FrameTestKey]::SetForegroundWindow([IntPtr]$WindowHandle) }
  finally { if ($frameAttached) { [void][FrameTestKey]::AttachThreadInput($frameTestThread,$framePreviousThread,$false) } }
  Start-Sleep -Milliseconds 100
}
[uint32]$frameForegroundId = 0
[void][FrameTestKey]::GetWindowThreadProcessId([FrameTestKey]::GetForegroundWindow(),[ref]$frameForegroundId)
# Modifier release remains safe if the user changes foreground application.
if ($frameForegroundId -ne $frameOwnerId -and $Key -notin @('CtrlUp','ShiftUp')) {
  throw 'Foreground left the Frame test process; key delivery refused'
}
$frameVirtualKey = switch ($Key) {
  {$_ -in @('CtrlDown','CtrlUp')} { 162 }
  {$_ -in @('ShiftDown','ShiftUp')} { 160 }
  'Tab' { 9 }
  'Escape' { 27 }
  'Enter' { 13 }
}
if ($Key.EndsWith('Down')) { [FrameTestKey]::keybd_event($frameVirtualKey,0,0,[UIntPtr]::Zero) }
elseif ($Key.EndsWith('Up')) { [FrameTestKey]::keybd_event($frameVirtualKey,0,2,[UIntPtr]::Zero) }
else {
  [FrameTestKey]::keybd_event($frameVirtualKey,0,0,[UIntPtr]::Zero)
  [FrameTestKey]::keybd_event($frameVirtualKey,0,2,[UIntPtr]::Zero)
}
