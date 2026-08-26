<#
  The two things a synthetic pointer cannot answer.

  Everything else in this harness posts window messages, which never moves the
  physical cursor. Two questions genuinely need the real one:

    1. Does hover work through Frame's OWN routing under a real pointer?
       test-hover.ps1 drives hover through DevTools, because MainWindow calls
       TrackMouseEvent and TrackMouseEvent watches the real cursor — with the
       pointer elsewhere, Windows posts WM_MOUSELEAVE immediately and Frame
       correctly clears the hover a synthetic move had just set. That is a
       limit of the harness, and the only way to show it is a limit of the
       harness rather than a bug is to use a real pointer once.

    2. Do tooltips appear? Frame's chrome carries title attributes, and the
       chrome surfaces are off-screen browsers. CEF is explicit about what that
       means: "When window rendering is disabled the application is responsible
       for drawing tooltips and the return value is ignored."

  THIS SCRIPT MOVES THE PHYSICAL CURSOR. It is the only one that does, it is
  not part of test-all.ps1, and it puts the cursor back where it found it —
  including if it fails part-way.
#>
param([string]$Config = 'Release', [int]$Port = 9333)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\drive.ps1"

if (-not ('RealPointer' -as [type])) {
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class RealPointer {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
  [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT p);
  [DllImport("user32.dll")] public static extern bool EnumWindows(Proc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern int GetClassName(IntPtr h, StringBuilder s, int max);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern int GetWindowText(IntPtr h, StringBuilder s, int max);
  public delegate bool Proc(IntPtr h, IntPtr p);
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }

  // Every visible top-level window in the system whose class looks like a
  // tooltip. Tooltips are not children of the window they describe, so they
  // have to be found this way rather than by walking Frame's hierarchy.
  public static string TooltipWindows() {
    var found = new System.Collections.Generic.List<string>();
    EnumWindows(delegate(IntPtr h, IntPtr p) {
      if (!IsWindowVisible(h)) return true;
      var cls = new StringBuilder(256);
      GetClassName(h, cls, cls.Capacity);
      string c = cls.ToString();
      // Frame's own popup is deliberately excluded: its class contains
      // "Tooltip" because that is what it is, and this function exists to
      // find a PLATFORM tooltip competing with it.
      if (c == "FrameTooltipSurface") return true;
      if (c.IndexOf("tooltip", StringComparison.OrdinalIgnoreCase) >= 0) {
        var txt = new StringBuilder(512);
        GetWindowText(h, txt, txt.Capacity);
        uint pid; GetWindowThreadProcessId(h, out pid);
        found.Add(c + " pid=" + pid + " text='" + txt.ToString() + "'");
      }
      return true;
    }, IntPtr.Zero);
    return string.Join(" | ", found.ToArray());
  }
}
"@
}

$script:results = @()
function Record($name, $ok, $detail) {
  $script:results += [pscustomobject]@{ Test = $name; Pass = [bool]$ok; Detail = $detail }
  Write-Host ("[{0}] {1} - {2}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $name, $detail) `
    -ForegroundColor $(if ($ok) { 'Green' } else { 'Red' })
}

$origin = New-Object RealPointer+POINT
[void][RealPointer]::GetCursorPos([ref]$origin)
Write-Host ("cursor parked at {0},{1} — it will be put back" -f $origin.X, $origin.Y)

try {
  Stop-Frame
  $app = Start-Frame -Bottom -Config $Config -ExtraArgs @("--remote-debugging-port=$Port")
  $h = $app.Hwnd
  Start-Sleep -Milliseconds 1800

  $topbarWs = (Get-FrameTarget -UrlLike 'frame://topbar*' -Port $Port).webSocketDebuggerUrl
  $r = New-Object FrameDrive+RECT
  [void][FrameDrive]::GetWindowRect($h, [ref]$r)
  $scale = Get-FrameDpiScale $h

  function ScreenPointOf($selector) {
    $rect = Get-FrameRects -WsUrl $topbarWs -Selector $selector | Select-Object -First 1
    if (-not $rect) { return $null }
    # Client DIPs -> screen pixels. The window is frameless, so the client
    # origin IS the window origin.
    return @{
      X = $r.Left + [int]($rect.cx * $scale)
      Y = $r.Top  + [int]($rect.cy * $scale)
    }
  }

  function HoveredIn($ws) {
    return Invoke-FrameEval -WsUrl $ws -Expression @"
(function () {
  var el = document.querySelector('.tab:hover, .icon-button:hover, .new-tab:hover, .caption-button:hover');
  return el ? (el.id || el.className) : 'nothing';
})()
"@
  }

  # --- 1. hover through Frame's own routing --------------------------------
  foreach ($control in @(
    @{ sel = '#new-tab';       name = 'New Tab button' },
    @{ sel = '#nav-reload';    name = 'Reload button' },
    @{ sel = '#sidebar-toggle'; name = 'Sidebar toggle' }
  )) {
    $p = ScreenPointOf $control.sel
    if (-not $p) { Record ("{0} hover (real pointer)" -f $control.name) $false 'not found'; continue }

    # Approach in steps, the way a pointer actually arrives.
    [void][RealPointer]::SetCursorPos($p.X, ($p.Y + 120))
    Start-Sleep -Milliseconds 150
    [void][RealPointer]::SetCursorPos($p.X, ($p.Y + 40))
    Start-Sleep -Milliseconds 120
    [void][RealPointer]::SetCursorPos($p.X, $p.Y)
    Start-Sleep -Milliseconds 500

    $hovered = HoveredIn $topbarWs
    Record ("{0} hovers under a real pointer" -f $control.name) ($hovered -ne 'nothing') `
      ("':hover' matched: $hovered")
  }

  # --- 2. tooltips ---------------------------------------------------------
  # Parked on a control with a title attribute for well past the ~500ms the
  # platform waits before showing one.
  $p = ScreenPointOf '#new-tab'
  [void][RealPointer]::SetCursorPos(($p.X - 30), $p.Y)
  Start-Sleep -Milliseconds 300
  [void][RealPointer]::SetCursorPos($p.X, $p.Y)
  Start-Sleep -Seconds 3

  $title = Invoke-FrameEval -WsUrl $topbarWs `
    -Expression "(document.getElementById('new-tab') || {}).title || ''"

  # Frame draws its own, in a popup of its own class — so a system tooltip
  # window is exactly what should NOT be there. What should is a visible
  # FrameTooltipSurface rendering frame://tooltip with the title's text in it.
  $tipTarget = Get-FrameTarget -UrlLike 'frame://tooltip*' -Port $Port
  $shown = ''
  $visible = $false
  if ($tipTarget) {
    $shown = Invoke-FrameEval -WsUrl $tipTarget.webSocketDebuggerUrl -Expression @"
(function () {
  var el = document.getElementById('tip');
  if (!el) { return ''; }
  return (el.classList.contains('is-open') ? 'open:' : 'closed:') + el.textContent;
})()
"@
    $wins = [FrameDrive]::TopLevel([uint32]$app.Process.Id) |
            Where-Object { [FrameDrive]::ClassOf($_) -eq 'FrameTooltipSurface' -and
                           [FrameDrive]::IsWindowVisible($_) }
    $visible = @($wins).Count -ge 1
  }

  Record 'A tooltip appears for a chrome control' `
    ($visible -and $shown -like ("open:" + $title)) `
    ("title '$title'; tooltip page says '$shown'; visible popup = $visible")

  # And it must be the drawn one, not a platform tooltip fighting with it.
  $systemTips = [RealPointer]::TooltipWindows()
  Record 'No stray platform tooltip competes with it' (-not $systemTips) `
    $(if ($systemTips) { $systemTips } else { 'none, as expected' })

  # --- 3. leaving clears hover, under a real pointer ------------------------
  [void][RealPointer]::SetCursorPos($p.X, ($p.Y + 300))
  Start-Sleep -Milliseconds 600
  $after = HoveredIn $topbarWs
  Record 'Leaving clears hover under a real pointer' ($after -eq 'nothing') `
    ("':hover' matched: $after")
}
finally {
  # Always, including on failure. The cursor is the user's.
  [void][RealPointer]::SetCursorPos($origin.X, $origin.Y)
  Write-Host ("cursor restored to {0},{1}" -f $origin.X, $origin.Y)
  Stop-Frame
}

Write-Host "`n=== summary ===" -ForegroundColor Cyan
$script:results | Format-Table -AutoSize
$failures = @($script:results | Where-Object { -not $_.Pass }).Count
Write-Host ("{0} of {1} passed" -f ($script:results.Count - $failures), $script:results.Count)
