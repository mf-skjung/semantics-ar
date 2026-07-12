param(
    [string]$VMName = "SarTarget",
    [PSCredential]$Credential
)
$ErrorActionPreference = 'Stop'
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

$Store = "C:\Windows\System32\drivers\SemanticsAr"

# The private store grants Administrators only Read+Execute (SYSTEM-only writes, Const. VII.1.1), so
# inducing a tamper requires taking ownership first (the admin token holds SeTakeOwnershipPrivilege).
function Reload {
    VM { Stop-Service semantics_ar_service -Force -ErrorAction SilentlyContinue; Start-Sleep -Seconds 2
         fltmc load semantics_ar 2>&1 | Out-Null; Start-Sleep -Seconds 2
         Start-Service semantics_ar_service -ErrorAction SilentlyContinue; Start-Sleep -Seconds 5 }
}
function Status { (VM { & C:\sar\sarctl.exe status 2>&1 | Out-String }).Trim() }

Write-Host "`n=== XII.3 integrity-halt verification ($(Get-Date -Format 'HH:mm:ss')) ===" -ForegroundColor Cyan
Assert "minifilter live" ([bool](VM { [bool]((fltmc filters 2>$null) -match 'semantics_ar') }))
$target = VMArgs {
    param($dir)
    if (Test-Path "$dir\preserve.idx") { "$dir\preserve.idx" }
    elseif (Test-Path "$dir\keystore.bin") { "$dir\keystore.bin" }
    else { "" }
} @($Store)
Metric "tamper target" $target
if (-not $target) {
    Assert "a MAC-chained store file exists to tamper" $false "run vm_verify_new first to populate the store"
    Write-Host "`n=== integrity-halt verification: $pass passed, $fail failed ===" -ForegroundColor Cyan
    exit 1
}

# ── DETECTION: flip a byte inside the MAC-chained index while the driver is unloaded; on reload the
#    load-verify (SarPreserveLoad / SarKeystoreLoad) fails the chain and latches the halt (VII.1.3). ──
VM { fltmc unload semantics_ar 2>&1 | Out-Null; Start-Sleep -Seconds 3 }
Assert "driver unloads cleanly (no bugcheck)" ([bool](VM { -not ((fltmc filters 2>$null) -match 'semantics_ar') }))
$flipped = VMArgs {
    param($p)
    & takeown /f $p 2>&1 | Out-Null
    & icacls $p /grant "$($env:USERNAME):F" 2>&1 | Out-Null
    try { $b=[IO.File]::ReadAllBytes($p); $b[$b.Length-1]=[byte]($b[$b.Length-1] -bxor 0xFF); [IO.File]::WriteAllBytes($p,$b); $true }
    catch { $false }
} @($target)
Assert "tamper: store byte flipped (took ownership)" ([bool]$flipped)
Reload
$s1 = Status
Metric "status (post-tamper)" $s1
Assert "post-tamper: driver reconnected" ($s1 -match 'driver_connected=1')
Assert "XII.3: integrity_halt=1 after store tamper/rollback" ($s1 -match 'integrity_halt=1')

# ── NO FALSE POSITIVE + the latch clears: remove the tampered store; a fresh empty/clean store loads
#    with no halt (an absent/empty store is not tamper, VII.1.3 / XII.3.2). ──
VM { fltmc unload semantics_ar 2>&1 | Out-Null; Start-Sleep -Seconds 3 }
VMArgs {
    param($dir)
    & takeown /f $dir /r /d y 2>&1 | Out-Null
    & icacls $dir /grant "$($env:USERNAME):F" /t 2>&1 | Out-Null
    Get-ChildItem $dir -Recurse -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
} @($Store)
Reload
$s2 = Status
Metric "status (clean store)" $s2
Assert "no false positive: clean/empty store stays integrity_halt=0" ($s2 -match 'integrity_halt=0')

Write-Host "`n=== integrity-halt verification: $pass passed, $fail failed ===" -ForegroundColor Cyan
if ($fail -gt 0) { exit 1 }
