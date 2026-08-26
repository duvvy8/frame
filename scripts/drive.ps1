<#
  Synthetic input harness for Frame.

  Drives a running Frame window ENTIRELY through posted window messages. The
  physical cursor is never moved, no window is ever foregrounded, and
  SetCursorPos / mouse_event / SendInput are not used anywhere in this file —
  which is what makes it safe to run against a browser on a second monitor
  while the primary display is in full-screen use.

  Frame's own WndProc is what forwards WM_MOUSEMOVE / WM_LBUTTONDOWN and
  friends into the off-screen chrome surfaces, so posting those messages to the
  window is not an approximation of a click: it is the same code path a real
  click takes, entered one step earlier.

  Coordinates are DIPs (the space every layout constant and every CSS surface
  works in). They are scaled to physical pixels here using the window's own DPI.

  Usage:
    . scripts\drive.ps1              # dot-source, then call the functions
    $h = Get-FrameWindow
    Move-FrameMouse $h 300 16
    Click-FrameMouse $h 300 16
    Send-FrameKey   $h 'T' -Ctrl
    Save-FrameShot  $h out.png
#>

Add-Type -AssemblyName System.Drawing, System.Windows.Forms

if (-not ('FrameDrive' -as [type])) {
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;

public class FrameDrive {
  [DllImport("user32.dll", SetLastError = true)]
  public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
  [DllImport("user32.dll", SetLastError = true)]
  public static extern IntPtr SendMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint flags);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern int GetClassName(IntPtr h, StringBuilder s, int max);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern int GetWindowText(IntPtr h, StringBuilder s, int max);
  [DllImport("user32.dll")]
  public static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr p);
  [DllImport("user32.dll")]
  public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")]
  public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern IntPtr GetFocus();
  [DllImport("user32.dll")] public static extern short VkKeyScan(char ch);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);

  public delegate bool EnumProc(IntPtr h, IntPtr p);

  [StructLayout(LayoutKind.Sequential)]
  public struct RECT { public int Left, Top, Right, Bottom; }

  public static System.Collections.Generic.List<IntPtr> Children(IntPtr parent) {
    var list = new System.Collections.Generic.List<IntPtr>();
    EnumChildWindows(parent, delegate(IntPtr h, IntPtr p) { list.Add(h); return true; }, IntPtr.Zero);
    return list;
  }

  public static System.Collections.Generic.List<IntPtr> TopLevel(uint pid) {
    var list = new System.Collections.Generic.List<IntPtr>();
    EnumWindows(delegate(IntPtr h, IntPtr p) {
      uint owner; GetWindowThreadProcessId(h, out owner);
      if (owner == pid) list.Add(h);
      return true;
    }, IntPtr.Zero);
    return list;
  }

  public static string ClassOf(IntPtr h) {
    var sb = new StringBuilder(256);
    GetClassName(h, sb, sb.Capacity);
    return sb.ToString();
  }
}
"@
}

$script:WM_MOUSEMOVE   = 0x0200
$script:WM_LBUTTONDOWN = 0x0201
$script:WM_LBUTTONUP   = 0x0202
$script:WM_RBUTTONDOWN = 0x0204
$script:WM_RBUTTONUP   = 0x0205
$script:WM_MBUTTONDOWN = 0x0207
$script:WM_MBUTTONUP   = 0x0208
$script:WM_MOUSEWHEEL  = 0x020A
$script:WM_MOUSELEAVE  = 0x02A3
$script:WM_KEYDOWN     = 0x0100
$script:WM_KEYUP       = 0x0101
$script:WM_CHAR        = 0x0102

function script:LParam([int]$x, [int]$y) {
  return [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF))
}

<# The Frame top-level window, by class name. Returns $null if not running. #>
function Get-FrameWindow {
  param([int]$ProcessId = 0, [int]$TimeoutSec = 20)
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    $procs = if ($ProcessId) { @(Get-Process -Id $ProcessId -ErrorAction SilentlyContinue) }
             else { @(Get-Process frame -ErrorAction SilentlyContinue) }
    foreach ($p in $procs) {
      foreach ($h in [FrameDrive]::TopLevel([uint32]$p.Id)) {
        if ([FrameDrive]::ClassOf($h) -eq 'FrameMainWindow' -and
            [FrameDrive]::IsWindowVisible($h)) { return $h }
      }
    }
    Start-Sleep -Milliseconds 250
  }
  return $null
}

<# Every Frame main window currently open, in creation order. #>
function Get-FrameWindows {
  $out = @()
  foreach ($p in @(Get-Process frame -ErrorAction SilentlyContinue)) {
    foreach ($h in [FrameDrive]::TopLevel([uint32]$p.Id)) {
      if ([FrameDrive]::ClassOf($h) -eq 'FrameMainWindow' -and
          [FrameDrive]::IsWindowVisible($h)) { $out += $h }
    }
  }
  return $out
}

function Get-FrameDpiScale {
  param([IntPtr]$Hwnd)
  $dpi = [FrameDrive]::GetDpiForWindow($Hwnd)
  if ($dpi -le 0) { return 1.0 }
  return $dpi / 96.0
}

<# DIP -> physical, using the window's own DPI, exactly as MainWindow does. #>
function script:Phys {
  param([IntPtr]$Hwnd, [double]$Dip)
  return [int][Math]::Round($Dip * (Get-FrameDpiScale $Hwnd))
}

function Move-FrameMouse {
  param([IntPtr]$Hwnd, [double]$X, [double]$Y, [int]$Steps = 1, [int]$DelayMs = 8)
  # Multi-step moves matter: hover transitions, :hover CSS and mouseenter all
  # need more than one sample to behave the way they do under a real pointer.
  $px = Phys $Hwnd $X; $py = Phys $Hwnd $Y
  if ($Steps -le 1) {
    [void][FrameDrive]::PostMessage($Hwnd, $script:WM_MOUSEMOVE, [IntPtr]::Zero, (LParam $px $py))
  } else {
    for ($i = 1; $i -le $Steps; $i++) {
      $fx = [int]($px * $i / $Steps); $fy = [int]($py * $i / $Steps)
      [void][FrameDrive]::PostMessage($Hwnd, $script:WM_MOUSEMOVE, [IntPtr]::Zero, (LParam $fx $fy))
      Start-Sleep -Milliseconds $DelayMs
    }
  }
  Start-Sleep -Milliseconds $DelayMs
}

<# Glides between two DIP points, one message per frame at ~60Hz. #>
function Glide-FrameMouse {
  param([IntPtr]$Hwnd, [double]$X1, [double]$Y1, [double]$X2, [double]$Y2,
        [int]$Frames = 12, [int]$DelayMs = 16)
  for ($i = 0; $i -le $Frames; $i++) {
    $t = $i / [double]$Frames
    $x = Phys $Hwnd ($X1 + ($X2 - $X1) * $t)
    $y = Phys $Hwnd ($Y1 + ($Y2 - $Y1) * $t)
    [void][FrameDrive]::PostMessage($Hwnd, $script:WM_MOUSEMOVE, [IntPtr]::Zero, (LParam $x $y))
    Start-Sleep -Milliseconds $DelayMs
  }
}

function Click-FrameMouse {
  param([IntPtr]$Hwnd, [double]$X, [double]$Y,
        [ValidateSet('Left','Right','Middle')][string]$Button = 'Left',
        [int]$SettleMs = 120)
  $px = Phys $Hwnd $X; $py = Phys $Hwnd $Y
  $lp = LParam $px $py
  # Hover first. A surface that has never seen the pointer at this position
  # will not have run its :hover / mouseenter work, and some controls only
  # become interactive once they have.
  [void][FrameDrive]::PostMessage($Hwnd, $script:WM_MOUSEMOVE, [IntPtr]::Zero, $lp)
  Start-Sleep -Milliseconds 40
  switch ($Button) {
    'Left'   { $down = $script:WM_LBUTTONDOWN; $up = $script:WM_LBUTTONUP;  $wp = [IntPtr]1 }
    'Right'  { $down = $script:WM_RBUTTONDOWN; $up = $script:WM_RBUTTONUP;  $wp = [IntPtr]2 }
    'Middle' { $down = $script:WM_MBUTTONDOWN; $up = $script:WM_MBUTTONUP;  $wp = [IntPtr]16 }
  }
  [void][FrameDrive]::PostMessage($Hwnd, $down, $wp, $lp)
  Start-Sleep -Milliseconds 30
  [void][FrameDrive]::PostMessage($Hwnd, $up, [IntPtr]::Zero, $lp)
  Start-Sleep -Milliseconds $SettleMs
}

<# Drags with intermediate moves, so drag handlers see a real gesture. #>
function Drag-FrameMouse {
  param([IntPtr]$Hwnd, [double]$X1, [double]$Y1, [double]$X2, [double]$Y2,
        [int]$Frames = 14, [int]$DelayMs = 16)
  $p1 = LParam (Phys $Hwnd $X1) (Phys $Hwnd $Y1)
  [void][FrameDrive]::PostMessage($Hwnd, $script:WM_MOUSEMOVE, [IntPtr]::Zero, $p1)
  Start-Sleep -Milliseconds 40
  [void][FrameDrive]::PostMessage($Hwnd, $script:WM_LBUTTONDOWN, [IntPtr]1, $p1)
  Start-Sleep -Milliseconds 40
  for ($i = 1; $i -le $Frames; $i++) {
    $t = $i / [double]$Frames
    $x = Phys $Hwnd ($X1 + ($X2 - $X1) * $t)
    $y = Phys $Hwnd ($Y1 + ($Y2 - $Y1) * $t)
    # wParam carries MK_LBUTTON so the surface knows the button is still held.
    [void][FrameDrive]::PostMessage($Hwnd, $script:WM_MOUSEMOVE, [IntPtr]1, (LParam $x $y))
    Start-Sleep -Milliseconds $DelayMs
  }
  $p2 = LParam (Phys $Hwnd $X2) (Phys $Hwnd $Y2)
  [void][FrameDrive]::PostMessage($Hwnd, $script:WM_LBUTTONUP, [IntPtr]::Zero, $p2)
  Start-Sleep -Milliseconds 120
}

function Scroll-FrameMouse {
  param([IntPtr]$Hwnd, [double]$X, [double]$Y, [int]$Delta = -120)
  # WM_MOUSEWHEEL carries SCREEN coordinates, unlike every other mouse message.
  $r = New-Object FrameDrive+RECT
  [void][FrameDrive]::GetWindowRect($Hwnd, [ref]$r)
  $sx = $r.Left + (Phys $Hwnd $X); $sy = $r.Top + (Phys $Hwnd $Y)
  $wp = [IntPtr](([int]$Delta -shl 16))
  [void][FrameDrive]::PostMessage($Hwnd, $script:WM_MOUSEWHEEL, $wp, (LParam $sx $sy))
  Start-Sleep -Milliseconds 80
}

$script:VK = @{
  'BACK'=0x08; 'TAB'=0x09; 'RETURN'=0x0D; 'ESCAPE'=0x1B; 'SPACE'=0x20
  'PRIOR'=0x21; 'NEXT'=0x22; 'END'=0x23; 'HOME'=0x24
  'LEFT'=0x25; 'UP'=0x26; 'RIGHT'=0x27; 'DOWN'=0x28; 'DELETE'=0x2E
  'F4'=0x73; 'F5'=0x74; 'F6'=0x75; 'F11'=0x7A; 'F12'=0x7B
  'OEM_PLUS'=0xBB; 'OEM_COMMA'=0xBC; 'OEM_MINUS'=0xBD
}

function script:VkOf([string]$Key) {
  if ($script:VK.ContainsKey($Key.ToUpper())) { return $script:VK[$Key.ToUpper()] }
  if ($Key.Length -eq 1) { return [int][char]([string]$Key).ToUpper() }
  throw "Unknown key '$Key'"
}

<#
  Posts a chord to a window.

  Modifier state is a problem for posted messages: WM_KEYDOWN carries no
  modifier flags, and both Frame and CEF read them with GetKeyState, which
  reports the REAL keyboard. SetKeyboardState only affects the calling thread's
  queue, so it cannot be used from here either.

  The honest answer is to post the modifier's own WM_KEYDOWN first and its
  WM_KEYUP last. That is what a real keyboard sends, and it is what CEF's
  message handler builds its modifier mask from for a windowed browser.
#>
function Send-FrameKey {
  param([IntPtr]$Hwnd, [string]$Key,
        [switch]$Ctrl, [switch]$Shift, [switch]$Alt,
        [int]$SettleMs = 140)
  $vk = VkOf $Key
  $scan = [FrameDrive]::MapVirtualKey([uint32]$vk, 0)
  $lpDown = [IntPtr](1 -bor ([int]$scan -shl 16))
  $lpUp   = [IntPtr](1 -bor ([int]$scan -shl 16) -bor (1 -shl 30) -bor (1 -shl 31))

  if ($Ctrl)  { [void][FrameDrive]::PostMessage($Hwnd, $script:WM_KEYDOWN, [IntPtr]0x11, [IntPtr]1) }
  if ($Shift) { [void][FrameDrive]::PostMessage($Hwnd, $script:WM_KEYDOWN, [IntPtr]0x10, [IntPtr]1) }
  if ($Alt)   { [void][FrameDrive]::PostMessage($Hwnd, $script:WM_KEYDOWN, [IntPtr]0x12, [IntPtr]1) }
  Start-Sleep -Milliseconds 15

  [void][FrameDrive]::PostMessage($Hwnd, $script:WM_KEYDOWN, [IntPtr]$vk, $lpDown)
  Start-Sleep -Milliseconds 25
  [void][FrameDrive]::PostMessage($Hwnd, $script:WM_KEYUP, [IntPtr]$vk, $lpUp)

  if ($Alt)   { [void][FrameDrive]::PostMessage($Hwnd, $script:WM_KEYUP, [IntPtr]0x12, [IntPtr]0xC0000001) }
  if ($Shift) { [void][FrameDrive]::PostMessage($Hwnd, $script:WM_KEYUP, [IntPtr]0x10, [IntPtr]0xC0000001) }
  if ($Ctrl)  { [void][FrameDrive]::PostMessage($Hwnd, $script:WM_KEYUP, [IntPtr]0x11, [IntPtr]0xC0000001) }
  Start-Sleep -Milliseconds $SettleMs
}

<# Types a literal string as WM_CHAR, which is what a focused text field reads. #>
function Send-FrameText {
  param([IntPtr]$Hwnd, [string]$Text, [int]$PerCharMs = 18)
  foreach ($ch in $Text.ToCharArray()) {
    [void][FrameDrive]::PostMessage($Hwnd, $script:WM_CHAR, [IntPtr][int][char]$ch, [IntPtr]1)
    Start-Sleep -Milliseconds $PerCharMs
  }
}

<# PrintWindow capture. Never foregrounds and never activates the window. #>
function Save-FrameShot {
  param([IntPtr]$Hwnd, [string]$Path, [int]$SettleMs = 400)
  Start-Sleep -Milliseconds $SettleMs
  $r = New-Object FrameDrive+RECT
  if (-not [FrameDrive]::GetWindowRect($Hwnd, [ref]$r)) { return $false }
  $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
  if ($w -le 0 -or $h -le 0) { return $false }
  $bmp = New-Object System.Drawing.Bitmap($w, $h)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  # Flag 2 = PW_RENDERFULLCONTENT, required for a composited/DWM window.
  $ok = [FrameDrive]::PrintWindow($Hwnd, $hdc, 2)
  $g.ReleaseHdc($hdc)
  if ($ok) {
    $dir = Split-Path -Parent $Path
    if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
    $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
  }
  $g.Dispose(); $bmp.Dispose()
  return $ok
}

<# Frame's own process tree: browser + every renderer/gpu/utility child. #>
function Get-FrameProcessStats {
  $procs = @(Get-Process frame -ErrorAction SilentlyContinue)
  $sum = 0L
  foreach ($p in $procs) { $sum += $p.WorkingSet64 }
  return [pscustomobject]@{
    Count     = $procs.Count
    TotalMB   = [Math]::Round($sum / 1MB, 1)
    Pids      = ($procs | ForEach-Object { $_.Id }) -join ','
  }
}

function Stop-Frame {
  Get-Process frame -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
  Start-Sleep -Milliseconds 600
}

<#
  Starts Frame on a non-primary monitor without activating it.

  -Bottom puts it in the lower half of that monitor, which is where this
  project's testing convention keeps it.
#>
function Start-Frame {
  param([switch]$Bottom, [string]$Config = 'Release', [string[]]$ExtraArgs = @(),
        [int]$WaitSec = 25)
  $repo = Split-Path -Parent $PSScriptRoot
  if (-not $repo) { $repo = (Get-Location).Path }
  $exe = Join-Path $repo "build\src\browser\$Config\frame.exe"
  if (-not (Test-Path $exe)) { throw "Frame not built: $exe" }

  $screen = [System.Windows.Forms.Screen]::AllScreens |
            Where-Object { -not $_.Primary } | Select-Object -First 1
  if (-not $screen) { $screen = [System.Windows.Forms.Screen]::PrimaryScreen }
  $b = $screen.Bounds

  $w = [Math]::Min(1280, $b.Width - 40)
  $h = [Math]::Min(760, [int]($b.Height / 2) - 20)
  $x = $b.X + [int](($b.Width - $w) / 2)
  $y = if ($Bottom) { $b.Y + [int]($b.Height / 2) + 8 } else { $b.Y + 10 }

  $a = @("--x=$x", "--y=$y", "--width=$w", "--height=$h", '--no-activate') + $ExtraArgs
  $p = Start-Process -FilePath $exe -ArgumentList $a -PassThru
  $hwnd = Get-FrameWindow -ProcessId $p.Id -TimeoutSec $WaitSec
  if (-not $hwnd) { throw "Frame window never appeared (pid $($p.Id))" }
  # The chrome surfaces are separate browsers and paint a beat after the window.
  Start-Sleep -Milliseconds 2200
  return [pscustomobject]@{ Process = $p; Hwnd = $hwnd }
}

<# True while the process is alive and the window still exists. #>
function Test-FrameAlive {
  param([IntPtr]$Hwnd)
  if (-not (Get-Process frame -ErrorAction SilentlyContinue)) { return $false }
  return [FrameDrive]::IsWindow($Hwnd)
}

# --- DevTools protocol ------------------------------------------------------
#
# Used for INSPECTION ONLY: element rectangles, computed styles, console
# errors, and the state a surface believes it is in. Input still goes through
# posted window messages, so the code path under test is Frame's own routing
# and not a DevTools shortcut around it.
#
# Frame has to be started with --remote-debugging-port=<port> for any of this
# to answer.

function Get-FrameTargets {
  param([int]$Port = 9333, [int]$TimeoutSec = 15)
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    try {
      return Invoke-RestMethod -Uri "http://127.0.0.1:$Port/json/list" -TimeoutSec 5
    } catch { Start-Sleep -Milliseconds 400 }
  }
  throw "No DevTools endpoint on port $Port"
}

function Get-FrameTarget {
  param([string]$UrlLike, [int]$Port = 9333)
  $all = Get-FrameTargets -Port $Port
  return ($all | Where-Object { $_.url -like $UrlLike -and $_.type -eq 'page' } | Select-Object -First 1)
}

<#
  Evaluates an expression in a target and returns the JSON-decoded result.

  One connection per call. That is slower than holding a socket open, but it
  cannot leave a half-open connection behind when a step fails, and these calls
  are made a few times per test rather than in a loop.
#>
function Invoke-FrameEval {
  param([string]$WsUrl, [string]$Expression, [int]$TimeoutSec = 15)
  $ws = New-Object System.Net.WebSockets.ClientWebSocket
  $cts = New-Object System.Threading.CancellationTokenSource ([TimeSpan]::FromSeconds($TimeoutSec))
  try {
    $ws.ConnectAsync([Uri]$WsUrl, $cts.Token).Wait()
    $msg = @{
      id = 1; method = 'Runtime.evaluate'
      params = @{ expression = $Expression; returnByValue = $true; awaitPromise = $true }
    } | ConvertTo-Json -Depth 8 -Compress
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($msg)
    $seg = New-Object System.ArraySegment[byte] (,$bytes)
    $ws.SendAsync($seg, 'Text', $true, $cts.Token).Wait()

    $buf = New-Object byte[] 262144
    $sb = New-Object System.Text.StringBuilder
    while ($true) {
      $rseg = New-Object System.ArraySegment[byte] (,$buf)
      $r = $ws.ReceiveAsync($rseg, $cts.Token)
      $r.Wait()
      [void]$sb.Append([System.Text.Encoding]::UTF8.GetString($buf, 0, $r.Result.Count))
      if ($r.Result.EndOfMessage) {
        $obj = $sb.ToString() | ConvertFrom-Json
        if ($obj.id -eq 1) {
          if ($obj.result.exceptionDetails) {
            throw ("eval threw: " + $obj.result.exceptionDetails.text)
          }
          return $obj.result.result.value
        }
        [void]$sb.Clear()   # an unsolicited event; keep reading
      }
    }
  } finally {
    try { $ws.Dispose() } catch {}
    $cts.Dispose()
  }
}

<# Bounding rects, in DIPs, of every element matching a selector. #>
function Get-FrameRects {
  param([string]$WsUrl, [string]$Selector)
  # The selector is JSON-encoded before it is spliced in, so a quote inside it
  # cannot break out of the expression being evaluated.
  $sel = $Selector | ConvertTo-Json -Compress
  $js = @"
JSON.stringify(Array.prototype.map.call(
  document.querySelectorAll($sel),
  function (el) {
    var r = el.getBoundingClientRect();
    return { x: r.x, y: r.y, w: r.width, h: r.height,
             cx: r.x + r.width / 2, cy: r.y + r.height / 2,
             id: el.id || '', cls: String(el.className || ''),
             tab: el.dataset ? (el.dataset.tabId || '') : '',
             text: (el.textContent || '').trim().slice(0, 40) };
  }))
"@
  $raw = Invoke-FrameEval -WsUrl $WsUrl -Expression $js
  if (-not $raw) { return }
  # Emitted one at a time on purpose. Windows PowerShell's ConvertFrom-Json
  # hands an array back as a SINGLE object, so `return @($parsed)` makes every
  # caller that writes @(Get-FrameRects ...).Count see 1 however many elements
  # there are. Writing each element to the pipeline is the only form that
  # counts correctly whether the caller collects it or pipes it.
  $parsed = $raw | ConvertFrom-Json
  foreach ($item in @($parsed)) { $item }
}

<#
  Topbar geometry, translated into WINDOW DIP coordinates.

  The topbar surface's origin is the window origin, so its client coordinates
  are already window coordinates. The sidebar's are not - see
  Get-FrameSidebarRects.
#>
function Get-FrameTopbarRects {
  param([string]$Selector, [int]$Port = 9333)
  $t = Get-FrameTarget -UrlLike 'frame://topbar*' -Port $Port
  if (-not $t) { throw 'topbar target not found' }
  return Get-FrameRects -WsUrl $t.webSocketDebuggerUrl -Selector $Selector
}

function Get-FrameSidebarRects {
  param([string]$Selector, [int]$Port = 9333, [int]$TopbarHeight = 32)
  $t = Get-FrameTarget -UrlLike 'frame://sidebar*' -Port $Port
  if (-not $t) { throw 'sidebar target not found' }
  $rects = @(Get-FrameRects -WsUrl $t.webSocketDebuggerUrl -Selector $Selector)
  foreach ($r in $rects) {
    $r.y  += $TopbarHeight
    $r.cy += $TopbarHeight
    $r
  }
}

<# The active page target, whatever it currently is. #>
function Get-FramePageTarget {
  param([int]$Port = 9333)
  $all = Get-FrameTargets -Port $Port
  return ($all | Where-Object {
    $_.type -eq 'page' -and $_.url -notlike 'frame://topbar*' -and
    $_.url -notlike 'frame://sidebar*'
  })
}

<#
  Generic DevTools call: method + params, one connection per call.

  Input.dispatchKeyEvent is the reason this exists. Frame reads Ctrl/Shift/Alt
  with GetKeyState, and GetKeyState reports the REAL keyboard - a POSTED
  WM_KEYDOWN for VK_CONTROL never enters it. So a posted chord arrives with no
  modifiers and matches nothing, which is a limitation of the harness and not
  of the browser. DevTools carries the modifier bits explicitly, which makes it
  the only way to drive a chord without touching the physical keyboard.
#>
function Invoke-FrameCdp {
  param([string]$WsUrl, [string]$Method, [hashtable]$Params = @{}, [int]$TimeoutSec = 15)
  $ws = New-Object System.Net.WebSockets.ClientWebSocket
  $cts = New-Object System.Threading.CancellationTokenSource ([TimeSpan]::FromSeconds($TimeoutSec))
  try {
    $ws.ConnectAsync([Uri]$WsUrl, $cts.Token).Wait()
    $msg = @{ id = 1; method = $Method; params = $Params } | ConvertTo-Json -Depth 8 -Compress
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($msg)
    $ws.SendAsync((New-Object System.ArraySegment[byte] (,$bytes)), 'Text', $true, $cts.Token).Wait()
    $buf = New-Object byte[] 262144
    $sb = New-Object System.Text.StringBuilder
    # The reply is best-effort. A command can destroy the very target it was
    # sent to — Ctrl+W is exactly that — and the socket then closes before the
    # acknowledgement arrives. That is the command succeeding, so it must not
    # surface as an error and abort the caller.
    try {
      while ($true) {
        $r = $ws.ReceiveAsync((New-Object System.ArraySegment[byte] (,$buf)), $cts.Token)
        $r.Wait()
        [void]$sb.Append([System.Text.Encoding]::UTF8.GetString($buf, 0, $r.Result.Count))
        if ($r.Result.EndOfMessage) {
          $obj = $sb.ToString() | ConvertFrom-Json
          if ($obj.id -eq 1) { return $obj }
          [void]$sb.Clear()
        }
      }
    } catch { return $null }
  } finally { try { $ws.Dispose() } catch {}; $cts.Dispose() }
}

$script:CDP_ALT = 1; $script:CDP_CTRL = 2; $script:CDP_SHIFT = 8

<#
  Sends a chord to whichever surface currently owns the keyboard.

  -Surface page|topbar|sidebar picks the target explicitly; the default follows
  the active page, which is where a browser shortcut is normally pressed.
#>
function Send-FrameChord {
  param([string]$Key, [switch]$Ctrl, [switch]$Shift, [switch]$Alt,
        [ValidateSet('page','topbar','sidebar')][string]$Surface = 'page',
        [int]$Port = 9333, [int]$SettleMs = 700)

  $target = switch ($Surface) {
    'topbar'  { Get-FrameTarget -UrlLike 'frame://topbar*'  -Port $Port }
    'sidebar' { Get-FrameTarget -UrlLike 'frame://sidebar*' -Port $Port }
    default   {
      # The ACTIVE tab's target, matched by URL, rather than whichever page
      # happens to be listed first. After a burst of tab closures the first
      # entry can be a browser that is already being destroyed, and a key
      # dispatched to one of those is simply dropped — which reads as the
      # shortcut not working when the shortcut is fine.
      $pages = @(Get-FramePageTarget -Port $Port)
      $activeUrl = (Get-FrameTabs -Port $Port | Where-Object { $_.active } |
                    Select-Object -First 1).url
      $match = $null
      if ($activeUrl) {
        $match = $pages | Where-Object { $_.url -eq $activeUrl } | Select-Object -First 1
      }
      if ($match) { $match } else { $pages | Select-Object -First 1 }
    }
  }
  if (-not $target) { throw "No $Surface target on port $Port" }

  $vk = VkOf $Key
  $mods = 0
  if ($Ctrl)  { $mods = $mods -bor $script:CDP_CTRL }
  if ($Shift) { $mods = $mods -bor $script:CDP_SHIFT }
  if ($Alt)   { $mods = $mods -bor $script:CDP_ALT }

  $common = @{
    modifiers             = $mods
    windowsVirtualKeyCode = $vk
    nativeVirtualKeyCode  = $vk
    key                   = $Key
  }
  # rawKeyDown, matching what CEF delivers first for a real press. PageClient
  # and ChromeSurface both act on RAWKEYDOWN and ignore the rest, so sending
  # only this pair runs each command exactly once.
  #
  # The keyUp is best-effort. Ctrl+W closes the very target the key was sent
  # to, so the socket going away between the two halves is the command working,
  # not failing — and treating it as an error aborts the test that proved it.
  [void](Invoke-FrameCdp -WsUrl $target.webSocketDebuggerUrl -Method 'Input.dispatchKeyEvent' `
          -Params ($common + @{ type = 'rawKeyDown' }))
  Start-Sleep -Milliseconds 25
  try {
    [void](Invoke-FrameCdp -WsUrl $target.webSocketDebuggerUrl -Method 'Input.dispatchKeyEvent' `
            -Params ($common + @{ type = 'keyUp' }))
  } catch { }
  Start-Sleep -Milliseconds $SettleMs
}

<# How many tabs the topbar is currently showing. #>
function Get-FrameTabCount {
  param([int]$Port = 9333)
  return @(Get-FrameTopbarRects '.tab:not(.is-leaving)' -Port $Port).Count
}

<#
  Navigates the active tab, through the same bridge command the address bar
  sends.

  Deliberately not by synthesising keystrokes into the address field: that
  tests the field's own key handling, which is a different thing, and it fails
  for reasons that have nothing to do with whatever the caller was trying to
  set up.
#>
function Navigate-Frame {
  param([string]$Url, [int]$Port = 9333, [int]$SettleMs = 3000)
  $side = Get-FrameTarget -UrlLike 'frame://sidebar*' -Port $Port
  if (-not $side) { throw 'sidebar target not found' }
  $request = ('nav:go:' + $Url) | ConvertTo-Json -Compress
  $js = @"
new Promise(function (resolve) {
  window.cefQuery({
    request: $request, persistent: false,
    onSuccess: function (r) { resolve('ok'); },
    onFailure: function (c, m) { resolve('fail ' + c + ' ' + m); }
  });
})
"@
  $result = Invoke-FrameEval -WsUrl $side.webSocketDebuggerUrl -Expression $js
  Start-Sleep -Milliseconds $SettleMs
  return $result
}

<# The tab strip as the topbar currently shows it, including sleep state. #>
function Get-FrameTabs {
  param([int]$Port = 9333)
  $t = Get-FrameTarget -UrlLike 'frame://topbar*' -Port $Port
  if (-not $t) { return }
  $raw = Invoke-FrameEval -WsUrl $t.webSocketDebuggerUrl -Expression @"
JSON.stringify(Array.prototype.map.call(document.querySelectorAll('.tab:not(.is-leaving)'), function (el) {
  var r = el.getBoundingClientRect();
  return {
    id: Number(el.dataset.tabId), cx: r.x + r.width / 2, cy: r.y + r.height / 2,
    url: el.dataset.tabUrl || '',
    x: r.x, w: r.width,
    active: el.classList.contains('is-active'),
    asleep: el.classList.contains('is-asleep'),
    muted: el.classList.contains('is-muted'),
    title: (el.querySelector('.tab-title') || {}).textContent || ''
  };
}))
"@
  if (-not $raw) { return }
  foreach ($item in @($raw | ConvertFrom-Json)) { $item }
}

<#
  The tabs a pointer could actually hit.

  A crowded strip scrolls, and a tab scrolled out of it still reports a
  bounding rect — one that lies outside the strip, over the nav cluster or the
  caption buttons. Clicking there does not click the tab, so any test that
  aims at "the first tab" without checking is aiming at nothing.
#>
function Get-FrameVisibleTabs {
  param([int]$Port = 9333)
  $t = Get-FrameTarget -UrlLike 'frame://topbar*' -Port $Port
  if (-not $t) { return }
  $raw = Invoke-FrameEval -WsUrl $t.webSocketDebuggerUrl -Expression @"
(function () {
  var strip = document.getElementById('tab-list').getBoundingClientRect();
  var out = [];
  var tabs = document.querySelectorAll('.tab:not(.is-leaving)');
  for (var i = 0; i < tabs.length; i++) {
    var el = tabs[i];
    var r = el.getBoundingClientRect();
    // Fully inside, not merely overlapping: a tab half under the fade at the
    // edge is half a target, and aiming at its centre can land outside.
    if (r.left < strip.left || r.right > strip.right) { continue; }
    out.push({
      id: Number(el.dataset.tabId), url: el.dataset.tabUrl || '',
      cx: r.x + r.width / 2, cy: r.y + r.height / 2, x: r.x, w: r.width,
      active: el.classList.contains('is-active'),
      asleep: el.classList.contains('is-asleep'),
      title: (el.querySelector('.tab-title') || {}).textContent || ''
    });
  }
  return JSON.stringify(out);
})()
"@
  if (-not $raw) { return }
  foreach ($item in @($raw | ConvertFrom-Json)) { $item }
}

<#
  Hovers an element inside an OFF-SCREEN chrome surface, via DevTools.

  Posted WM_MOUSEMOVE cannot be used to observe hover, and the reason is
  Windows rather than Frame. MainWindow calls TrackMouseEvent on the first
  move so it can clear hover when the pointer leaves — and TrackMouseEvent
  watches the REAL cursor. With the physical cursor on another monitor (which
  is the entire point of this harness) Windows posts WM_MOUSELEAVE
  immediately, and Frame correctly clears the hover the synthetic move had
  just set. Under a real pointer the sequence never happens: the cursor is
  inside the window, so no leave is generated.

  So hover is driven through the surface's own input pipeline instead. Frame's
  ROUTING is proven separately and continuously — every click in these tests
  goes through posted messages — and this checks the half that routing hands
  off to: does the element under the pointer actually respond.
#>
function Hover-FrameSurface {
  param([string]$SurfaceUrlLike, [double]$X, [double]$Y, [int]$Port = 9333,
        [int]$SettleMs = 260)
  $t = Get-FrameTarget -UrlLike $SurfaceUrlLike -Port $Port
  if (-not $t) { throw "no target for $SurfaceUrlLike" }
  # Two moves: one to enter, one inside. A single event can be treated as a
  # jump and skip the enter transition a real pointer produces.
  foreach ($step in @(0.5, 1.0)) {
    [void](Invoke-FrameCdp -WsUrl $t.webSocketDebuggerUrl `
      -Method 'Input.dispatchMouseEvent' -Params @{
        type = 'mouseMoved'; x = [int]($X * $step); y = [int]($Y * $step); buttons = 0
      })
    Start-Sleep -Milliseconds 40
  }
  Start-Sleep -Milliseconds $SettleMs
}

<# Moves the pointer off everything in a surface, so hover resets. #>
function Unhover-FrameSurface {
  param([string]$SurfaceUrlLike, [int]$Port = 9333, [int]$SettleMs = 260)
  $t = Get-FrameTarget -UrlLike $SurfaceUrlLike -Port $Port
  if (-not $t) { return }
  [void](Invoke-FrameCdp -WsUrl $t.webSocketDebuggerUrl `
    -Method 'Input.dispatchMouseEvent' -Params @{
      type = 'mouseMoved'; x = -50; y = -50; buttons = 0
    })
  Start-Sleep -Milliseconds $SettleMs
}
