param([Parameter(Mandatory=$true)][long]$FrameHandle,
      [Parameter(Mandatory=$true)][long]$DialogHandle,
      [ValidateSet('list','filename','open','save','cancel')][string]$Action='list',
      [string]$Value)
$ErrorActionPreference='Stop'
# The installed Windows provider exposes filename/buttons as legacy panes.
# Use documented Win32 control messages only inside this owned file dialog.
Add-Type @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class FrameFileDialog {
  [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left,top,right,bottom; }
  public class Control { public long Handle; public string Class; public string Text; public int Id,X,Y,Width,Height; }
  public delegate bool EnumCallback(IntPtr window,IntPtr parameter);
  [DllImport("user32.dll")] static extern IntPtr GetWindow(IntPtr window,uint command);
  [DllImport("user32.dll")] static extern IntPtr GetParent(IntPtr window);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent,EnumCallback callback,IntPtr parameter);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
  [DllImport("user32.dll")] static extern bool IsWindowEnabled(IntPtr window);
  [DllImport("user32.dll")] static extern int GetDlgCtrlID(IntPtr window);
  [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr window,out Rect rectangle);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window,StringBuilder text,int capacity);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr window,StringBuilder text,int capacity);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr SendMessage(IntPtr window,uint message,IntPtr parameter,string text);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr SendMessage(IntPtr window,uint message,IntPtr parameter,StringBuilder text);
  [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr window,uint message,IntPtr parameter,IntPtr value);
  static string Text(IntPtr window){var text=new StringBuilder(4096);GetWindowText(window,text,text.Capacity);return text.ToString();}
  public static void CheckOwner(IntPtr dialog,IntPtr frame){
    var owner=dialog;
    for(int depth=0;depth<16;++depth){owner=GetWindow(owner,4);if(owner==frame)return;if(owner==IntPtr.Zero)break;}
    throw new InvalidOperationException("File dialog is not owned by the isolated Frame window");
  }
  public static List<Control> Controls(IntPtr dialog){
    var result=new List<Control>();
    EnumChildWindows(dialog,(window,parameter)=>{
      if(!IsWindowVisible(window)||!IsWindowEnabled(window))return true;
      var name=new StringBuilder(128);GetClassName(window,name,name.Capacity);
      string kind=name.ToString();
      if(kind!="Edit"&&kind!="Button"&&kind!="Static"&&kind!="ComboBox"&&kind!="ComboBoxEx32")return true;
      Rect rect;GetWindowRect(window,out rect);
      result.Add(new Control{Handle=window.ToInt64(),Class=kind,Text=(kind=="Static"||kind=="Button")?Text(window):"",Id=GetDlgCtrlID(window),X=rect.left,Y=rect.top,Width=rect.right-rect.left,Height=rect.bottom-rect.top});return true;
    },IntPtr.Zero);return result;
  }
  public static void Filename(IntPtr dialog,string value){
    var controls=Controls(dialog);
    var label=controls.Find(c=>c.Class=="Static"&&c.Text.Replace("&","").Trim().Equals("File name:",StringComparison.OrdinalIgnoreCase));
    List<Control> edits;
    if(label!=null){
      edits=controls.FindAll(c=>c.Class=="Edit"&&Math.Abs(c.Y-label.Y)<25&&c.Width>100&&c.X>label.X);
    }else{
      // IFileSaveDialog renders the label in DirectUI, while its single
      // filename editor remains a standard lower-row ComboBox child (1001).
      Rect rect;GetWindowRect(dialog,out rect);
      edits=controls.FindAll(c=>{
        if(c.Class!="Edit"||c.Id!=1001||c.Y<rect.top+(rect.bottom-rect.top)/2||c.Width<100)return false;
        var parentClass=new StringBuilder(128);GetClassName(GetParent(new IntPtr(c.Handle)),parentClass,parentClass.Capacity);
        return parentClass.ToString()=="ComboBox";
      });
    }
    if(edits.Count!=1)throw new InvalidOperationException("Native filename editor is ambiguous or missing");
    var edit=new IntPtr(edits[0].Handle);
    SendMessage(edit,0x000C,IntPtr.Zero,value); // WM_SETTEXT, ordinary file-dialog editor.
    var actual=new StringBuilder(4096);
    SendMessage(edit,0x000D,new IntPtr(actual.Capacity),actual); // WM_GETTEXT across processes.
    if(actual.ToString()!=value)throw new InvalidOperationException("Native filename editor rejected value");
  }
  public static void Click(IntPtr dialog,string name){
    var buttons=Controls(dialog).FindAll(c=>c.Class=="Button"&&c.Text.Replace("&","").Trim().Equals(name,StringComparison.OrdinalIgnoreCase));
    if(buttons.Count!=1)throw new InvalidOperationException("Native dialog button is ambiguous or missing");
    SendMessage(new IntPtr(buttons[0].Handle),0x00F5,IntPtr.Zero,IntPtr.Zero); // BM_CLICK.
  }
}
'@
[FrameFileDialog]::CheckOwner([IntPtr]$DialogHandle,[IntPtr]$FrameHandle)
switch($Action){
  'list' { [FrameFileDialog]::Controls([IntPtr]$DialogHandle) | ConvertTo-Json -Compress }
  'filename' { [FrameFileDialog]::Filename([IntPtr]$DialogHandle,$Value) }
  'open' { [FrameFileDialog]::Click([IntPtr]$DialogHandle,'Open') }
  'save' { [FrameFileDialog]::Click([IntPtr]$DialogHandle,'Save') }
  'cancel' { [FrameFileDialog]::Click([IntPtr]$DialogHandle,'Cancel') }
}
