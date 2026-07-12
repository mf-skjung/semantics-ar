param([string]$Name = "shot", [int]$SettleSeconds = 9, [string]$Nav = "")
$app = "c:\Users\skjun\OneDrive\Documents\repos\semantics-ar\frontend\SemanticsAr.App\bin\Release\net10.0-windows10.0.19041.0\win-x64\SemanticsAr.App.exe"
Get-Process SemanticsAr.App -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep 1
$p = Start-Process $app -PassThru
Start-Sleep $SettleSeconds
$p.Refresh()
if ($Nav) {
    try {
        Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
        $root = [System.Windows.Automation.AutomationElement]::RootElement
        $wc = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, "semantics-ar")
        $win = $root.FindFirst([System.Windows.Automation.TreeScope]::Children, $wc)
        if ($win) {
            $tc = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty, [System.Windows.Automation.ControlType]::ListItem)
            $nc = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $Nav)
            $both = New-Object System.Windows.Automation.AndCondition($tc, $nc)
            $item = $win.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $both)
            if ($item) { $item.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select(); Start-Sleep 2 }
            else { Write-Host "nav '$Nav' not found" }
        }
    } catch { Write-Host "nav exc: $_" }
}
Add-Type @"
using System;using System.Runtime.InteropServices;
public static class W{ [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h); [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int c);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r); public struct RECT{public int L,T,R,B;} }
"@
Add-Type -AssemblyName System.Drawing
if ($p.MainWindowHandle -ne 0) {
    [W]::ShowWindow($p.MainWindowHandle, 9) | Out-Null
    [W]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
    Start-Sleep 1
    $r = New-Object W+RECT; [W]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
    $w = $r.R - $r.L; $h = $r.B - $r.T
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size($w, $h)))
    $out = "c:\Users\skjun\OneDrive\Documents\repos\semantics-ar\build_verify\host_$Name.png"
    $bmp.Save($out); $g.Dispose(); $bmp.Dispose()
    Write-Host "saved $out ($w x $h)"
} else {
    Write-Host "NO WINDOW HANDLE"
}
Stop-Process $p -Force -ErrorAction SilentlyContinue
