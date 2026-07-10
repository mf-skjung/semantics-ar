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
function Metric { param([string]$Name,[string]$Value) Write-Host ("  METRIC  {0,-44} {1}" -f $Name,$Value) -ForegroundColor Cyan }

Write-Host "`n=== self-protection store-anchor verification ($(Get-Date -Format 'HH:mm:ss')) ===" -ForegroundColor Cyan
Assert "minifilter live" ([bool](VM { [bool]((fltmc filters 2>$null) -match 'semantics_ar') }))
Assert "service running" ([bool](VM { (Get-Service semantics_ar_service -ErrorAction SilentlyContinue).Status -eq 'Running' }))

$writeProbe = {
    param($Path)
    $mkdir = Split-Path -Parent $Path
    try { if ($mkdir -and -not (Test-Path $mkdir)) { New-Item -ItemType Directory -Force $mkdir | Out-Null } } catch {}
    $wrote = $false
    try {
        [System.IO.File]::WriteAllText($Path, "sar-selfprotect-probe")
        $wrote = (Test-Path $Path) -and ([System.IO.File]::ReadAllText($Path) -eq "sar-selfprotect-probe")
    } catch { $wrote = $false }
    $wrote
}

# ── The reported bug: a bare-substring match on "SemanticsAr" write-blocked the product's OWN
#    frontend binaries and any unrelated file whose name merely contains the product name, anywhere
#    on disk. After anchoring to the store LOCATION, these must all write successfully. ──
$appOk    = VMArgs $writeProbe @("C:\Windows\Temp\SemanticsAr.App.exe")
$hostOk   = VMArgs $writeProbe @("C:\Windows\Temp\SemanticsArElevationHost.exe")
$coreOk   = VMArgs $writeProbe @("C:\Users\Public\SemanticsAr.Core.dll")
Metric "write C:\...\Temp\SemanticsAr.App.exe" $appOk
Assert "over-match fixed: SemanticsAr.App.exe writable outside store"        ([bool]$appOk)
Assert "over-match fixed: SemanticsArElevationHost.exe writable outside store" ([bool]$hostOk)
Assert "over-match fixed: SemanticsAr.Core.dll writable outside store"       ([bool]$coreOk)

# ── Component boundary: a sibling whose name merely starts with the store component must NOT be
#    treated as store (this is the class of over-match a bare prefix/substring would still hit). ──
$siblingDrivers = VMArgs $writeProbe @("C:\SemanticsArQuarantineElsewhere\probe.txt")
$siblingName    = VMArgs $writeProbe @("C:\Users\Public\SemanticsArQuarantine_notthedir.txt")
Assert "boundary: C:\SemanticsArQuarantineElsewhere\ is not protected" ([bool]$siblingDrivers)
Assert "boundary: SemanticsArQuarantine_notthedir.txt is not protected" ([bool]$siblingName)

# ── No regression: the real store subtree under \System32\drivers\SemanticsAr\ must stay
#    tamper-proof even for a local administrator (VII.1.1). ──
$storeDir = "C:\Windows\System32\drivers\SemanticsAr"
$renameBlocked = VMArgs {
    param($Dir)
    if (-not (Test-Path $Dir)) { return $false }
    try { Rename-Item -Path $Dir -NewName "SemanticsAr_tamper" -ErrorAction Stop; $moved = $true } catch { $moved = $false }
    # A correct block leaves the store directory exactly where it was.
    (-not $moved) -and (Test-Path $Dir) -and (-not (Test-Path (Join-Path (Split-Path -Parent $Dir) "SemanticsAr_tamper")))
} @($storeDir)
Assert "no-regression: store directory rename is blocked" ([bool]$renameBlocked)

$storeWriteBlocked = VMArgs {
    param($Dir)
    $p = Join-Path $Dir "sar_tamper_probe.bin"
    $blocked = $true
    try {
        [System.IO.File]::WriteAllText($p, "tamper")
        if ((Test-Path $p) -and ([System.IO.File]::ReadAllText($p) -eq "tamper")) { $blocked = $false }
    } catch { $blocked = $true }
    $blocked
} @($storeDir)
Assert "no-regression: write into store subtree is blocked" ([bool]$storeWriteBlocked)

# ── No under-match: the per-volume namespace-preservation quarantine dir (\<volume>\
#    SemanticsArQuarantine\) holds preserved originals and must remain protected wherever it lives. ──
$quarBlocked = VMArgs {
    param()
    $dir = "C:\SemanticsArQuarantine"
    try { if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null } } catch {}
    $p = Join-Path $dir "0000000000000001.q"
    $blocked = $true
    try {
        [System.IO.File]::WriteAllText($p, "tamper")
        if ((Test-Path $p) -and ([System.IO.File]::ReadAllText($p) -eq "tamper")) { $blocked = $false }
    } catch { $blocked = $true }
    $blocked
} @()
Assert "no-under-match: write into \<volume>\SemanticsArQuarantine\ is blocked" ([bool]$quarBlocked)

Write-Host "`n=== self-protection verification: $pass passed, $fail failed ===" -ForegroundColor Cyan
if ($fail -gt 0) { exit 1 }
