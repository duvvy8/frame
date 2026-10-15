param([Parameter(Mandatory=$true)][long]$WindowHandle,
      [ValidateSet('list','listall','windows','invoke','select','focus','setvalue','reveal','contextmenu','middleclick','menu','cancelmenu','resize','maximise','restore','minimise','close')][string]$Action = 'list', [string]$Name, [string]$Value, [string]$MenuName, [double]$MinX = -1, [double]$MinY = -1, [double]$Width = 800, [double]$Height = 500)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$frameWindow = [System.Windows.Automation.AutomationElement]::FromHandle([IntPtr]$WindowHandle)
if (-not $frameWindow) { throw 'Frame test window missing' }
if ($Action -eq 'windows') {
  Add-Type @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class FrameOwnedWindows {
  [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left,top,right,bottom; }
  public delegate bool EnumCallback(IntPtr window, IntPtr parameter);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumCallback callback, IntPtr parameter);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
  [DllImport("user32.dll")] static extern IntPtr GetWindow(IntPtr window, uint command);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rectangle);
  static bool IsOwnedBy(IntPtr window, IntPtr ownerWindow) {
    for (int depth=0; depth<16; ++depth) {
      window=GetWindow(window,4); // GW_OWNER, including brokered native dialogs.
      if(window==IntPtr.Zero)return false;
      if(window==ownerWindow)return true;
    }
    return false;
  }
  public static List<IntPtr> Visible(uint owner, IntPtr ownerWindow) {
    var result = new List<IntPtr>();
    EnumWindows((window, parameter) => {
      uint process; GetWindowThreadProcessId(window, out process);
      if ((process == owner || IsOwnedBy(window,ownerWindow)) && IsWindowVisible(window)) result.Add(window);
      return true;
    }, IntPtr.Zero);
    return result;
  }
}
'@
  foreach ($frameOwnedHandle in [FrameOwnedWindows]::Visible($frameWindow.Current.ProcessId,[IntPtr]$WindowHandle)) {
    $frameOwnedName = ''
    try { $frameOwnedName = [System.Windows.Automation.AutomationElement]::FromHandle($frameOwnedHandle).Current.Name } catch {}
    $frameOwnedRect = [FrameOwnedWindows+Rect]::new()
    [void][FrameOwnedWindows]::GetWindowRect($frameOwnedHandle,[ref]$frameOwnedRect)
    [pscustomobject]@{Name=$frameOwnedName; Handle=$frameOwnedHandle.ToInt64(); X=$frameOwnedRect.left; Y=$frameOwnedRect.top; Width=($frameOwnedRect.right-$frameOwnedRect.left); Height=($frameOwnedRect.bottom-$frameOwnedRect.top)}
  }
} elseif ($Action -eq 'menu') {
  # Follow the real native application menu. Value is a JSON array of labels,
  # so site titles containing punctuation need no custom delimiter escaping.
  $frameMenuPath = @($Value | ConvertFrom-Json)
  if (-not $frameMenuPath.Count) { throw 'Native menu path is empty' }
  $frameGear = $frameWindow.FindFirst([System.Windows.Automation.TreeScope]::Descendants,
    [System.Windows.Automation.PropertyCondition]::new(
      [System.Windows.Automation.AutomationElement]::NameProperty,'Settings and browser menu'))
  if (-not $frameGear) { throw 'Frame browser menu button missing' }
  $frameGear.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
  for ($framePathIndex = 0; $framePathIndex -lt $frameMenuPath.Count; $framePathIndex++) {
    $frameMenuLabel = [string]$frameMenuPath[$framePathIndex]
    $frameMenuCondition = [System.Windows.Automation.AndCondition]::new(
      [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty,$frameMenuLabel),
      [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ClassNameProperty,'MenuItemView'))
    $frameMenuTarget = $null
    for ($frameAttempt = 0; $frameAttempt -lt 30 -and -not $frameMenuTarget; $frameAttempt++) {
      Start-Sleep -Milliseconds 100
      $frameMenuTarget = $frameWindow.FindFirst([System.Windows.Automation.TreeScope]::Descendants,$frameMenuCondition)
    }
    if (-not $frameMenuTarget -or -not $frameMenuTarget.Current.IsEnabled) { throw "Native menu item unavailable: $frameMenuLabel" }
    $frameExpansion = $null
    if ($frameMenuTarget.TryGetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern,[ref]$frameExpansion)) {
      $frameExpansion.Expand()
    } else {
      if ($framePathIndex -ne $frameMenuPath.Count - 1) { throw "Native submenu missing: $frameMenuLabel" }
      $frameMenuTarget.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
    }
  }
} elseif ($Action -eq 'cancelmenu') {
  Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class FrameCancelMenu {
  [DllImport("user32.dll", SetLastError=true)] public static extern bool PostMessage(IntPtr window,uint message,IntPtr parameter,IntPtr point);
}
'@
  if (-not [FrameCancelMenu]::PostMessage([IntPtr]$WindowHandle,0x001F,[IntPtr]::Zero,[IntPtr]::Zero)) { throw 'Native menu cancellation failed' }
} elseif ($Action -eq 'resize') {
  $frameTransform = $frameWindow.GetCurrentPattern([System.Windows.Automation.TransformPattern]::Pattern)
  if (-not $frameTransform.Current.CanResize) { throw 'Frame window cannot resize' }
  $frameTransform.Resize($Width,$Height)
} elseif ($Action -in @('maximise','restore','minimise','close')) {
  $framePattern = $frameWindow.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern)
  if ($Action -eq 'close') { $framePattern.Close() }
  else {
    $frameState = switch ($Action) {
      'maximise' { [System.Windows.Automation.WindowVisualState]::Maximized }
      'minimise' { [System.Windows.Automation.WindowVisualState]::Minimized }
      'restore' { [System.Windows.Automation.WindowVisualState]::Normal }
    }
    $framePattern.SetWindowVisualState($frameState)
    if ($framePattern.Current.WindowVisualState -ne $frameState) { throw 'Window state did not change' }
  }
} elseif ($Action -in @('list','listall')) {
  $frameCondition = if ($Action -eq 'listall') { [System.Windows.Automation.Condition]::TrueCondition } else {
    [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ControlTypeProperty,[System.Windows.Automation.ControlType]::Button)
  }
  $frameButtons = $frameWindow.FindAll([System.Windows.Automation.TreeScope]::Descendants,
    $frameCondition)
  foreach ($frameButton in $frameButtons) {
    if ([double]::IsInfinity($frameButton.Current.BoundingRectangle.X)) { continue }
    $frameControlName = [string]$frameButton.Current.Name
    $frameSelection = $null
    $frameSelected = $null
    if ($frameButton.TryGetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern,[ref]$frameSelection)) { $frameSelected = $frameSelection.Current.IsSelected }
    [pscustomobject]@{Name=$frameControlName; Type=$frameButton.Current.ControlType.ProgrammaticName; Class=$frameButton.Current.ClassName; Handle=$frameButton.Current.NativeWindowHandle; Enabled=$frameButton.Current.IsEnabled; Offscreen=$frameButton.Current.IsOffscreen; Selected=$frameSelected; X=$frameButton.Current.BoundingRectangle.X; Y=$frameButton.Current.BoundingRectangle.Y; Width=$frameButton.Current.BoundingRectangle.Width; Height=$frameButton.Current.BoundingRectangle.Height}
  }
} else {
  $frameMatches = $frameWindow.FindAll([System.Windows.Automation.TreeScope]::Descendants,
    [System.Windows.Automation.PropertyCondition]::new(
      [System.Windows.Automation.AutomationElement]::NameProperty, $Name))
  $frameTarget = $frameMatches | Where-Object { ($Action -ne 'contextmenu' -or $_.Current.ControlType -eq [System.Windows.Automation.ControlType]::TabItem) -and ($Action -ne 'setvalue' -or $_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Edit) -and ($MinX -eq -1 -or $_.Current.BoundingRectangle.X -ge $MinX) -and ($MinY -eq -1 -or $_.Current.BoundingRectangle.Y -ge $MinY) } | Select-Object -First 1
  if (-not $frameTarget -and $Action -in @('contextmenu','invoke','reveal','focus','middleclick')) {
    # Dock accessible names include title, domain and state, unlike the former
    # strip's title-only names. Preserve exact targeting for the COM call.
    $frameTarget = $frameWindow.FindAll([System.Windows.Automation.TreeScope]::Descendants,
      [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ControlTypeProperty,[System.Windows.Automation.ControlType]::TabItem)) |
      Where-Object { $_.Current.Name.StartsWith($Name + "`n") } | Select-Object -First 1
    if ($frameTarget) { $Name = $frameTarget.Current.Name }
  }
  if (-not $frameTarget) { throw "Frame control missing: $Name" }
  if (-not $frameTarget.Current.IsEnabled) { throw "Frame control disabled: $Name" }
  if ($Action -eq 'middleclick') {
    if ($frameTarget.Current.ClassName -ne 'FrameDockTabButton' -or $frameTarget.Current.IsOffscreen) { throw 'Middle click requires a visible native dock tab' }
    if ((Get-Process -Id $frameWindow.Current.ProcessId).ProcessName -ne 'Frame') { throw 'Expected isolated Frame window' }
    # Deliver OS mouse messages only to this test HWND, without moving the
    # global pointer or changing the foreground application.
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class FrameDockMiddleClick {
  [StructLayout(LayoutKind.Sequential)] public struct Point { public int x,y; }
  [DllImport("user32.dll")] static extern bool ScreenToClient(IntPtr window,ref Point point);
  [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr window,uint message,IntPtr parameter,IntPtr point);
  public static void Click(IntPtr window,int x,int y) {
    var point=new Point { x=x,y=y };
    if(!ScreenToClient(window,ref point))throw new InvalidOperationException("Could not map dock coordinates");
    var packed=new IntPtr((point.y<<16)|(point.x&0xffff));
    SendMessage(window,0x0207,new IntPtr(0x10),packed);
    SendMessage(window,0x0208,IntPtr.Zero,packed);
  }
}
'@
    $frameTabBounds = $frameTarget.Current.BoundingRectangle
    [FrameDockMiddleClick]::Click([IntPtr]$WindowHandle,[int]($frameTabBounds.X+$frameTabBounds.Width/2),[int]($frameTabBounds.Y+$frameTabBounds.Height/2))
  } elseif ($Action -eq 'contextmenu') {
    if ($frameTarget.Current.ControlType -ne [System.Windows.Automation.ControlType]::TabItem) { throw 'Context-menu test target must be a native tab' }
    $frameInteropDirectory = Join-Path $PSScriptRoot '../profiles/test-tools'
    $frameInteropPath = Join-Path $frameInteropDirectory 'Interop.FrameUIAutomation.dll'
    if (-not (Test-Path -LiteralPath $frameInteropPath)) {
      New-Item -ItemType Directory -Path $frameInteropDirectory -Force | Out-Null
      $frameImporter = 'C:\Program Files (x86)\Microsoft SDKs\Windows\v10.0A\bin\NETFX 4.8.1 Tools\x64\TlbImp.exe'
      if (-not (Test-Path -LiteralPath $frameImporter)) { throw 'Windows SDK type-library importer is required for native context-menu acceptance' }
      & $frameImporter 'C:\Windows\System32\UIAutomationCore.dll' "/out:$frameInteropPath" '/namespace:FrameUIAutomation' '/machine:X64' *> (Join-Path $frameInteropDirectory 'interop-build.log')
      if ($LASTEXITCODE -ne 0) { throw 'Windows UI Automation interop generation failed' }
    }
    Add-Type -Path $frameInteropPath
    Add-Type @'
using System;
using FrameUIAutomation;
public static class FrameNativeTabMenu {
  public static void Show(IntPtr window,string name) {
    IUIAutomation automation=new CUIAutomationClass();
    var root=automation.ElementFromHandle(window);
    var condition=automation.CreateAndCondition(automation.CreatePropertyCondition(30005,name),automation.CreatePropertyCondition(30003,50019));
    var tab=root.FindFirst(TreeScope.TreeScope_Descendants,condition);
    if(tab==null)throw new InvalidOperationException("Native context-menu tab missing");
    ((IUIAutomationElement3)tab).ShowContextMenu();
  }
}
'@ -ReferencedAssemblies $frameInteropPath
    [FrameNativeTabMenu]::Show([IntPtr]$WindowHandle,$Name)
    if ($MenuName) {
      $frameMenuCondition = [System.Windows.Automation.AndCondition]::new(
        [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty,$MenuName),
        [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ClassNameProperty,'MenuItemView'))
      $frameMenuTarget = $null
      for ($frameMenuAttempt=0; $frameMenuAttempt -lt 20 -and -not $frameMenuTarget; $frameMenuAttempt++) {
        Start-Sleep -Milliseconds 100
        $frameMenuTarget = $frameWindow.FindFirst([System.Windows.Automation.TreeScope]::Descendants,$frameMenuCondition)
      }
      if (-not $frameMenuTarget) { throw "Frame menu action missing: $MenuName" }
      if (-not $frameMenuTarget.Current.IsEnabled) { throw "Frame menu action disabled: $MenuName" }
      $frameMenuInvoke = $null
      if ($frameMenuTarget.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern,[ref]$frameMenuInvoke)) {
        $frameMenuInvoke.Invoke()
      } else {
        $frameMenuTarget.GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern).Toggle()
      }
    }
  } elseif ($Action -eq 'reveal') {
    $frameScrollItem = $frameTarget.GetCurrentPattern([System.Windows.Automation.ScrollItemPattern]::Pattern)
    $frameScrollItem.ScrollIntoView()
  } elseif ($Action -eq 'focus') {
    $frameTarget.SetFocus()
  } elseif ($Action -eq 'setvalue') {
    $frameValue = $frameTarget.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)
    if ($frameValue.Current.IsReadOnly) { throw "Frame control is read-only: $Name" }
    $frameValue.SetValue($Value)
  } elseif ($Action -eq 'select') {
    $frameSelect = $frameTarget.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern)
    $frameSelect.Select()
  } else {
    $frameInvoke = $null
    $frameExpand = $null
    $frameToggle = $null
    if ($frameTarget.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern,[ref]$frameInvoke)) {
      $frameInvoke.Invoke()
    } elseif ($frameTarget.TryGetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern,[ref]$frameExpand)) {
      $frameExpand.Expand()
    } elseif ($frameTarget.TryGetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern,[ref]$frameToggle)) {
      $frameToggle.Toggle()
    } else { throw "Frame control has no supported invocation pattern: $Name" }
  }
}
