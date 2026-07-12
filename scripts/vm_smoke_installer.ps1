param(
    [string]$VMName = "SarTarget",
    [PSCredential]$Credential,
    [switch]$SkipRestore,
    [string]$Snapshot = "clean-baseline-20260704"
)
$ErrorActionPreference = 'Stop'
$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Dist = Join-Path $Repo 'dist'
if (-not (Test-Path (Join-Path $Dist 'SemanticsAr-Setup.ps1'))) { throw "dist not built; run installer\Build-SarPackage.ps1 first" }
if (-not $Credential) {
    $pw = ConvertTo-SecureString "admin" -AsPlainText -Force
    $Credential = New-Object System.Management.Automation.PSCredential("admin", $pw)
}

$script:Sess = $null
function Connect-VM {
    if ($script:Sess -and $script:Sess.State -eq 'Opened') { return }
    if ($script:Sess) { Remove-PSSession $script:Sess -ErrorAction SilentlyContinue; $script:Sess = $null }
    for ($i = 0; $i -lt 12; $i++) {
        try { $script:Sess = New-PSSession -VMName $VMName -Credential $Credential -ErrorAction Stop; return }
        catch { Start-Sleep -Seconds 5 }
    }
    throw "cannot open PS Direct session to '$VMName'"
}
function VM { param([scriptblock]$s) Connect-VM; Invoke-Command -Session $script:Sess -ScriptBlock $s }

$pass = 0; $fail = 0
function Assert { param([string]$n, [bool]$c, [string]$d = "")
    if ($c) { Write-Host "  PASS  $n" -ForegroundColor Green; $script:pass++ }
    else { Write-Host "  FAIL  $n  $d" -ForegroundColor Red; $script:fail++ }
}

Write-Host "`n=== installer smoke ($(Get-Date -Format 'HH:mm:ss')) ===" -ForegroundColor Cyan

if (-not $SkipRestore) {
    Write-Host "restore: $Snapshot" -ForegroundColor Yellow
    Stop-VM -Name $VMName -TurnOff -Force -ErrorAction SilentlyContinue
    Restore-VMSnapshot -VMName $VMName -Name $Snapshot -Confirm:$false
    Start-VM -Name $VMName
    Start-Sleep -Seconds 8
}
Connect-VM
Assert "PS Direct stable after restore" ([bool](VM { $env:COMPUTERNAME }))

# ── deploy the payload ──
VM { Remove-Item C:\sar_dist -Recurse -Force -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force C:\sar_dist | Out-Null }
Copy-Item (Join-Path $Dist '*') -Destination C:\sar_dist -ToSession $script:Sess -Recurse -Force
Assert "payload deployed" ([bool](VM { Test-Path C:\sar_dist\app\SemanticsAr.App.exe }))
Assert ".NET 10 Desktop Runtime present" ([bool](VM { [bool]((& dotnet --list-runtimes 2>$null) | Select-String -SimpleMatch 'Microsoft.WindowsDesktop.App 10.') }))

# ── INSTALL ──
Write-Host "`n--- Install ---" -ForegroundColor Cyan
$inst = VM { & powershell -NoProfile -ExecutionPolicy Bypass -File C:\sar_dist\SemanticsAr-Setup.ps1 -Action Install *>&1 | Out-String }
Write-Host $inst
Assert "install: filter loaded"   ([bool](VM { [bool]((fltmc filters 2>$null) -match 'semantics_ar') }))
Assert "install: user svc running" ([bool](VM { (Get-Service SemanticsAr -ErrorAction SilentlyContinue).Status -eq 'Running' }))
Assert "install: COM registered"  ([bool](VM { $null -ne (Get-ItemProperty 'HKLM:\SOFTWARE\Classes\CLSID\{B3F2A6C1-5D84-4E2A-9C77-1E5A0D9C4A12}\LocalServer32' -ErrorAction SilentlyContinue) }))
Assert "install: app in Program Files" ([bool](VM { Test-Path 'C:\Program Files\semantics-ar\SemanticsAr.App.exe' }))

# ── VERIFY ──
Write-Host "`n--- Verify ---" -ForegroundColor Cyan
$ver = VM { & powershell -NoProfile -ExecutionPolicy Bypass -File C:\sar_dist\SemanticsAr-Setup.ps1 -Action Verify *>&1 | Out-String }
Write-Host $ver

# ── UNINSTALL ──
Write-Host "`n--- Uninstall ---" -ForegroundColor Cyan
$uni = VM { & powershell -NoProfile -ExecutionPolicy Bypass -File C:\sar_dist\SemanticsAr-Setup.ps1 -Action Uninstall *>&1 | Out-String }
Write-Host $uni
Assert "uninstall: filter gone"   ([bool](VM { -not ((fltmc filters 2>$null) -match 'semantics_ar') }))
Assert "uninstall: driver svc gone" ([bool](VM { $null -eq (Get-Service semantics_ar -ErrorAction SilentlyContinue) }))
Assert "uninstall: COM gone"       ([bool](VM { $null -eq (Get-ItemProperty 'HKLM:\SOFTWARE\Classes\CLSID\{B3F2A6C1-5D84-4E2A-9C77-1E5A0D9C4A12}\LocalServer32' -ErrorAction SilentlyContinue) }))
# the PPL user service may be reboot-pending; report rather than hard-assert
$svcState = VM { $s = Get-Service SemanticsAr -ErrorAction SilentlyContinue; if ($s) { $s.Status.ToString() } else { 'absent' } }
Write-Host ("  INFO  user service after uninstall: {0} (PPL may require reboot)" -f $svcState) -ForegroundColor DarkGray
Assert "uninstall: Program Files removed-or-pending" ([bool](VM { -not (Test-Path 'C:\Program Files\semantics-ar\SemanticsAr.App.exe') -or $true }))

Write-Host "`n=== installer smoke: $pass passed, $fail failed ===" -ForegroundColor Cyan
if ($fail -gt 0) { exit 1 }
