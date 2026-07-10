param(
    [string]$VMName = "SarTarget",
    [PSCredential]$Credential,
    [string]$PublishDir = (Join-Path $env:TEMP "sar_app_publish"),
    [string]$InteractiveUser = "admin",
    [switch]$SkipPublish
)
$ErrorActionPreference = 'Stop'
$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $Credential) {
    $pw = ConvertTo-SecureString "admin" -AsPlainText -Force
    $Credential = New-Object System.Management.Automation.PSCredential("admin", $pw)
}
$script:Sess = $null
function Connect-VM {
    if ($script:Sess -and $script:Sess.State -eq 'Opened') { return }
    if ($script:Sess) { Remove-PSSession $script:Sess -ErrorAction SilentlyContinue; $script:Sess = $null }
    for ($i = 0; $i -lt 10; $i++) {
        try { $script:Sess = New-PSSession -VMName $VMName -Credential $Credential -ErrorAction Stop; return }
        catch { Start-Sleep -Seconds 5 }
    }
    throw "Cannot open PowerShell Direct session to VM '$VMName'."
}
function VM { param([scriptblock]$Script) Connect-VM; Invoke-Command -Session $script:Sess -ScriptBlock $Script }
function VMArgs { param([scriptblock]$Script,[object[]]$Arguments) Connect-VM; Invoke-Command -Session $script:Sess -ScriptBlock $Script -ArgumentList $Arguments }

$pass = 0; $fail = 0
function Assert { param([string]$Name,[bool]$Cond,[string]$Detail="")
    if ($Cond) { Write-Host "  PASS  $Name" -ForegroundColor Green; $script:pass++ }
    else { Write-Host "  FAIL  $Name  $Detail" -ForegroundColor Red; $script:fail++ }
}
function Metric { param([string]$Name,[string]$Value) Write-Host ("  METRIC  {0,-40} {1}" -f $Name,$Value) -ForegroundColor Cyan }

Write-Host "`n=== live GUI on VM ($(Get-Date -Format 'HH:mm:ss')) ===" -ForegroundColor Cyan

# ── Publish framework-dependent, then overlay the ABI-matched native surface ──
if (-not $SkipPublish) {
    Write-Host "Publish: SemanticsAr.App (framework-dependent)" -ForegroundColor Yellow
    if (Test-Path $PublishDir) { Get-ChildItem $PublishDir -Recurse | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue }
    & dotnet publish (Join-Path $Repo "frontend\SemanticsAr.App\SemanticsAr.App.csproj") -c Release -o $PublishDir --nologo -v quiet
    if ($LASTEXITCODE -ne 0) { throw "dotnet publish failed" }
}
$freshApi = Join-Path $Repo "build_win\frontend\sarapi\Release\sarapi.dll"
if (Test-Path $freshApi) { Copy-Item $freshApi (Join-Path $PublishDir "sarapi.dll") -Force }
$elevHost = Join-Path $Repo "build_win\frontend\elevation-host\Release\SemanticsArElevationHost.exe"
if (Test-Path $elevHost) { Copy-Item $elevHost (Join-Path $PublishDir "SemanticsArElevationHost.exe") -Force }
Assert "publish produced SemanticsAr.App.exe" (Test-Path (Join-Path $PublishDir "SemanticsAr.App.exe"))
Assert "ABI-matched sarapi.dll present" (Test-Path (Join-Path $PublishDir "sarapi.dll"))

# ── Deploy to the VM, clearing any leaked instances in every session first ──
Connect-VM
VM { Get-Process SemanticsAr.App -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue }
VM { if (Test-Path "C:\sar\app") { Get-ChildItem "C:\sar\app" -Recurse -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue } else { New-Item -ItemType Directory -Force "C:\sar\app" | Out-Null } }
Copy-Item (Join-Path $PublishDir "*") -Destination "C:\sar\app" -ToSession $script:Sess -Recurse -Force
Assert "app deployed to VM C:\sar\app" ([bool](VM { Test-Path "C:\sar\app\SemanticsAr.App.exe" }))

# ── Register the COM elevation host (LocalServer32) so the itemized surfaces can elevate ──
if (VM { Test-Path "C:\sar\app\SemanticsArElevationHost.exe" }) {
    VM { & "C:\sar\app\SemanticsArElevationHost.exe" /RegServer 2>&1 | Out-Null }
    $comOk = VM { $null -ne (Get-ItemProperty "HKLM:\SOFTWARE\Classes\CLSID\{B3F2A6C1-5D84-4E2A-9C77-1E5A0D9C4A12}\LocalServer32" -ErrorAction SilentlyContinue) }
    Assert "COM elevation host registered" ([bool]$comOk)
}

# ── Engine liveness so the app renders REAL data, not an empty shell ──
Assert "minifilter live" ([bool](VM { [bool]((fltmc filters 2>$null) -match 'semantics_ar') }))
Assert "service running" ([bool](VM { (Get-Service semantics_ar_service -ErrorAction SilentlyContinue).Status -eq 'Running' }))

# ── A composed, visible window only exists on an interactive desktop (WinSta0\Default). A headless
#    PowerShell Direct session is session 0, which has no such desktop; the window handle assertion is
#    meaningful only when an operator is logged on interactively (VMConnect Enhanced Session), or
#    unattended auto-logon is configured. Detect this up front instead of waiting on a doomed launch. ──
$interactive = VM { [bool](Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -ne 0 }) }
if (-not $interactive) {
    Write-Host "`n  NOTE  no interactive logon session on the VM." -ForegroundColor Yellow
    Write-Host "        The app is deployed and the engine is live, but a composed window can only be" -ForegroundColor Yellow
    Write-Host "        rendered/asserted on an interactive desktop. Connect the VMConnect Enhanced" -ForegroundColor Yellow
    Write-Host "        Session (log on as $InteractiveUser), then re-run this script; it will launch the" -ForegroundColor Yellow
    Write-Host "        app into that session and assert MainWindowHandle != 0 with a screenshot." -ForegroundColor Yellow
    Write-Host "`n=== live GUI: deploy OK, engine live; interactive session required for the render assertion ===" -ForegroundColor Cyan
    exit 2
}

# ── Launch in the operator's INTERACTIVE session (PowerShell Direct runs in session 0, which has no
#    desktop). A scheduled task with an Interactive principal lands on the logged-on user's desktop —
#    the only place a composed window actually appears. The launcher captures the window handle, the
#    title, a screenshot, and the navigation surface names as evidence. ──
$launcher = @'
$ErrorActionPreference = 'SilentlyContinue'
Get-Process SemanticsAr.App | Stop-Process -Force
Start-Sleep -Seconds 1
$p = Start-Process "C:\sar\app\SemanticsAr.App.exe" -PassThru
Start-Sleep -Seconds 12
$handle = 0; $title = ''; $nav = @()
$proc = Get-Process -Id $p.Id -ErrorAction SilentlyContinue
if ($proc) { $proc.Refresh(); $handle = [int64]$proc.MainWindowHandle; $title = $proc.MainWindowTitle }
try {
    Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
    $cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty, [System.Windows.Automation.ControlType]::ListItem)
    $root = [System.Windows.Automation.AutomationElement]::RootElement
    $wcond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, "semantics-ar")
    $win = $root.FindFirst([System.Windows.Automation.TreeScope]::Children, $wcond)
    if ($win) { $items = $win.FindAll([System.Windows.Automation.TreeScope]::Descendants, $cond); foreach ($it in $items) { $nav += $it.Current.Name } }
} catch {}
try {
    Add-Type -AssemblyName System.Drawing, System.Windows.Forms
    $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
    $bmp.Save("C:\sar\gui_evidence.png")
    $g.Dispose(); $bmp.Dispose()
} catch {}
[pscustomobject]@{ handle = $handle; title = $title; nav = $nav; procId = $p.Id } | ConvertTo-Json -Compress | Set-Content "C:\sar\gui_result.json"
'@
VMArgs { param($s) Set-Content -Path "C:\sar\gui_launch.ps1" -Value $s -Encoding UTF8 } @($launcher)
VM { Remove-Item "C:\sar\gui_result.json","C:\sar\gui_evidence.png" -Force -ErrorAction SilentlyContinue }

VMArgs {
    param($user)
    Unregister-ScheduledTask -TaskName "SarGuiLaunch" -Confirm:$false -ErrorAction SilentlyContinue
    $act = New-ScheduledTaskAction -Execute "powershell.exe" -Argument "-NonInteractive -ExecutionPolicy Bypass -File C:\sar\gui_launch.ps1"
    $prin = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Limited
    Register-ScheduledTask -TaskName "SarGuiLaunch" -Action $act -Principal $prin -Force | Out-Null
    Start-ScheduledTask -TaskName "SarGuiLaunch"
} @($InteractiveUser)

$deadline = (Get-Date).AddSeconds(60)
$result = $null
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 4
    $raw = VM { if (Test-Path "C:\sar\gui_result.json") { Get-Content "C:\sar\gui_result.json" -Raw } else { "" } }
    if ($raw) { $result = $raw | ConvertFrom-Json; break }
}
VM { Unregister-ScheduledTask -TaskName "SarGuiLaunch" -Confirm:$false -ErrorAction SilentlyContinue }

if ($null -eq $result) {
    Assert "GUI launcher produced a result" $false "no gui_result.json within 60s"
} else {
    Metric "MainWindowHandle" $result.handle
    Metric "MainWindowTitle" $result.title
    Metric "navigation surfaces" (($result.nav) -join ", ")
    Assert "window rendered on VM (MainWindowHandle != 0)" ([int64]$result.handle -ne 0) "handle=$($result.handle)"
    Assert "window title is 'semantics-ar'" ($result.title -eq "semantics-ar") "title=$($result.title)"
    Assert "Recovery budget surface present" ([bool](($result.nav) -contains "Recovery budget"))
    Assert "Exemptions surface present" ([bool](($result.nav) -contains "Exemptions"))
    $evi = Join-Path $Repo "build_verify\gui_evidence.png"
    try { Copy-Item -FromSession $script:Sess -Path "C:\sar\gui_evidence.png" -Destination $evi -Force; Metric "screenshot" $evi } catch {}
}

Write-Host "`n=== live GUI verification: $pass passed, $fail failed ===" -ForegroundColor Cyan
Write-Host "The app stays running (ShutdownMode=OnExplicitShutdown); view it in the VMConnect" -ForegroundColor DarkGray
Write-Host "Enhanced Session and click Home -> Recovery budget -> Exemptions against the live engine." -ForegroundColor DarkGray
if ($fail -gt 0) { exit 1 }
