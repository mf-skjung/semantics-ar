param(
    [string]$VMName = "SarTarget",
    [string]$Repo = (Join-Path $PSScriptRoot ".."),
    [PSCredential]$Credential,
    [switch]$SkipRestore,
    [switch]$SkipDeploy
)
$ErrorActionPreference = 'Stop'
$Repo = (Resolve-Path $Repo).Path

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
function VM { param([scriptblock]$Script)
    for ($a = 0; $a -lt 2; $a++) {
        try { Connect-VM; return Invoke-Command -Session $script:Sess -ScriptBlock $Script -ErrorAction Stop }
        catch { if ($a -eq 1) { throw }; $script:Sess = $null }
    }
}
function VMArgs { param([scriptblock]$Script,[object[]]$Arguments)
    for ($a = 0; $a -lt 2; $a++) {
        try { Connect-VM; return Invoke-Command -Session $script:Sess -ScriptBlock $Script -ArgumentList $Arguments -ErrorAction Stop }
        catch { if ($a -eq 1) { throw }; $script:Sess = $null }
    }
}
function CopyToVM { param([string]$Local,[string]$Remote) Connect-VM; Copy-Item -Path $Local -Destination $Remote -ToSession $script:Sess -Force }

$pass = 0; $fail = 0; $skip = 0
function Assert { param([string]$Name,[bool]$Cond,[string]$Detail="")
    if ($Cond) { Write-Host "  PASS  $Name" -ForegroundColor Green; $script:pass++ }
    else { Write-Host "  FAIL  $Name  $Detail" -ForegroundColor Red; $script:fail++ }
}
function Skip { param([string]$Name,[string]$Detail="") Write-Host "  SKIP  $Name  $Detail" -ForegroundColor DarkYellow; $script:skip++ }
function Metric { param([string]$Name,[string]$Value) Write-Host ("  METRIC  {0,-46} {1}" -f $Name, $Value) -ForegroundColor Cyan }

function LaunchHold { param([int]$Sec)
    VMArgs { param($s) (Start-Process -FilePath C:\sar\inject_probe.exe -ArgumentList "hold",$s -PassThru).Id } @($Sec)
}
function RunAttack { param([int]$Pid2)
    $o = VMArgs { param($p) & C:\sar\inject_probe.exe attack $p 2>&1 | Out-String } @($Pid2)
    $w = 0; $t = 0; $op = 0
    if ($o -match 'open=(\d)')   { $op = [int]$Matches[1] }
    if ($o -match 'write=(\d)')  { $w  = [int]$Matches[1] }
    if ($o -match 'thread=(\d)') { $t  = [int]$Matches[1] }
    [pscustomobject]@{ open=$op; write=$w; thread=$t; raw=$o.Trim() }
}
function SetMode { param([string]$M) VMArgs { param($m) & C:\sar\sarctl.exe mode $m 2>&1 | Out-Null } @($M) | Out-Null }

# ── VM restore ceremony (HANDOFF 5) ──────────────────────────────────────
if (-not $SkipRestore) {
    Write-Host "Restore: clean-baseline-20260704" -ForegroundColor Yellow
    Stop-VM -Name $VMName -TurnOff -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 6
    Restore-VMSnapshot -VMName $VMName -Name "clean-baseline-20260704" -Confirm:$false
    Start-VM -Name $VMName
    Start-Sleep -Seconds 45
    $script:Sess = $null
    Connect-VM
    Assert "PowerShell Direct stable after restore" ([bool](VM { $env:COMPUTERNAME }))
}

# ── Deploy (pre-signed package + T2 harness) ─────────────────────────────
if (-not $SkipDeploy) {
    Write-Host "Deploy: signed driver + service + sarctl + inject_probe" -ForegroundColor Yellow
    $pkg = "$Repo\build_driver\pkg"
    foreach ($f in @("semantics_ar.sys","semantics_ar.cat","semantics_ar.inf")) {
        if (-not (Test-Path "$pkg\$f")) { throw "signed package missing $f" }
    }
    try { VM { Stop-Service semantics_ar_service -Force -ErrorAction SilentlyContinue } } catch {}
    try { VM { fltmc unload semantics_ar 2>$null } } catch {}
    Start-Sleep -Seconds 2
    VM { $s="C:\Windows\System32\drivers\SemanticsAr"; if (Test-Path $s){ Get-ChildItem "$s" -Recurse -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue } }
    VM { if (-not (Test-Path "C:\sar")) { New-Item -ItemType Directory -Path "C:\sar" -Force | Out-Null } }
    foreach ($f in @("semantics_ar.sys","semantics_ar.inf","semantics_ar.cat","SemanticsArTest.cer")) {
        $src = Join-Path $pkg $f; if (Test-Path $src) { CopyToVM $src (Join-Path "C:\sar" $f) }
    }
    CopyToVM "$Repo\build_win\service\Release\semantics_ar_service.exe" "C:\sar\semantics_ar_service.exe"
    CopyToVM "$Repo\build_win\tools\Release\sarctl.exe" "C:\sar\sarctl.exe"
    foreach ($h in @("inject_probe.exe","stream_transform.exe")) {
        $src = "$Repo\build_harness\$h"; if (Test-Path $src) { CopyToVM $src "C:\sar\$h" }
    }
    VM {
        $cer = "C:\sar\SemanticsArTest.cer"
        if (Test-Path $cer) { certutil -addstore Root $cer 2>$null | Out-Null; certutil -addstore TrustedPublisher $cer 2>$null | Out-Null }
        pnputil /add-driver C:\sar\semantics_ar.inf /install 2>$null | Out-Null
        $svc = "HKLM:\SYSTEM\CurrentControlSet\Services\semantics_ar"
        New-Item -Path $svc -Force | Out-Null
        Set-ItemProperty $svc -Name ImagePath -Value "\??\C:\sar\semantics_ar.sys"
        Set-ItemProperty $svc -Name Type -Value 2 -Type DWord
        Set-ItemProperty $svc -Name Start -Value 3 -Type DWord
        Set-ItemProperty $svc -Name ErrorControl -Value 1 -Type DWord
        Set-ItemProperty $svc -Name Group -Value "FSFilter Activity Monitor"
        New-Item -Path "$svc\Instances" -Force | Out-Null
        Set-ItemProperty "$svc\Instances" -Name DefaultInstance -Value "semantics_ar Instance"
        New-Item -Path "$svc\Instances\semantics_ar Instance" -Force | Out-Null
        Set-ItemProperty "$svc\Instances\semantics_ar Instance" -Name Altitude -Value "385000"
        Set-ItemProperty "$svc\Instances\semantics_ar Instance" -Name Flags -Value 0 -Type DWord
    }
    $loadOut = VM { $o = fltmc load semantics_ar 2>&1; "exit=$LASTEXITCODE :: " + ($o -join ' ') }
    Write-Host "  load: $loadOut"
    Start-Sleep -Seconds 3
    $loaded = VM { [bool]((fltmc filters 2>$null) -match 'semantics_ar') }
    Assert "minifilter loaded (ObRegisterCallbacks succeeded: /integritycheck + signed)" ([bool]$loaded) "load: $loadOut"
    if (-not $loaded) { throw "minifilter failed to load" }
    VM {
        $p="C:\sar\semantics_ar_service.exe"
        if (-not (Get-Service semantics_ar_service -ErrorAction SilentlyContinue)) { New-Service -Name semantics_ar_service -BinaryPathName $p -StartupType Manual -ErrorAction SilentlyContinue | Out-Null }
        Start-Service semantics_ar_service -ErrorAction SilentlyContinue; Start-Sleep -Seconds 4
    }
    Assert "service running" ([bool](VM { (Get-Service semantics_ar_service -ErrorAction SilentlyContinue).Status -eq 'Running' }))
}
$live = VM { [bool]((fltmc filters 2>$null) -match 'semantics_ar') }
Assert "minifilter live at measurement start" ([bool]$live)
if (-not $live) { throw "driver not live" }

# ── Phase T2-FPCTL: non-exempt target is untouched by ObGuard (zero usability impact) ──
Write-Host "`nPhase T2-FPCTL: attack a NON-exempt target -> injection succeeds (ObGuard does not touch non-exempt)" -ForegroundColor Yellow
SetMode "enforce"
$fpPid = LaunchHold 25
Start-Sleep -Seconds 1
Metric "FPCTL victim pid (non-exempt)" "$fpPid"
$fp = RunAttack $fpPid
Metric "FPCTL attack result" $fp.raw
Assert "T2-FPCTL non-exempt target: injection SUCCEEDS (no ObGuard interference)" (($fp.write -eq 1) -and ($fp.thread -eq 1)) $fp.raw
VMArgs { param($p) Stop-Process -Id $p -Force -ErrorAction SilentlyContinue } @($fpPid) | Out-Null

# ── Phase T2-OBGUARD: exempt target -> injection stripped; query still works ──
Write-Host "`nPhase T2-OBGUARD: whitelist+verdict -> exempt target; untrusted injection is stripped, query survives" -ForegroundColor Yellow
$resolveOut = VM { & C:\sar\sarctl.exe resolve C:\sar\inject_probe.exe 2>&1 | Out-String }
$signedOk = $resolveOut -match 'verdict=verified'
Metric "OBGUARD inject_probe resolves" ($(if($signedOk){'verified'}else{'NOT-verified'}))
if (-not $signedOk) {
    Skip "T2-OBGUARD (inject_probe not trust-verified on VM; same gate as Phase TIER2)" "resolve: $($resolveOut.Trim())"
} else {
    # baseline: before exemption, injection into the victim succeeds
    $bPid = LaunchHold 40
    Start-Sleep -Seconds 1
    $base = RunAttack $bPid
    Metric "OBGUARD baseline (pre-exempt) attack" $base.raw
    Assert "T2-OBGUARD baseline: pre-exemption injection succeeds" (($base.write -eq 1) -and ($base.thread -eq 1)) $base.raw

    # exempt the victim: whitelist + verdict, confirm EXEMPT
    $wl = VM { & C:\sar\sarctl.exe whitelist-add C:\sar\inject_probe.exe 2>&1 | Out-String }
    Assert "T2-OBGUARD whitelist-add succeeds" ($wl -match 'result=0') "wl: $($wl.Trim())"
    $vout = VMArgs { param($p) & C:\sar\sarctl.exe verdict $p 2>&1 | Out-String } @($bPid)
    $pq   = VMArgs { param($p) & C:\sar\sarctl.exe procquery $p 2>&1 | Out-String } @($bPid)
    Metric "OBGUARD verdict / procquery" "$($vout.Trim()) || $($pq.Trim())"
    $isExempt = $pq -match 'id_state=2\(exempt\)'
    Assert "T2-OBGUARD victim is EXEMPT after verdict" ([bool]$isExempt) "procquery: $($pq.Trim())"

    if ($isExempt) {
        SetMode "enforce"
        $atk = RunAttack $bPid
        Metric "OBGUARD ENFORCE attack on exempt victim" $atk.raw
        Assert "T2-OBGUARD injection into exempt victim is BLOCKED (write=0)" ($atk.write -eq 0) $atk.raw
        Assert "T2-OBGUARD remote-thread into exempt victim is BLOCKED (thread=0)" ($atk.thread -eq 0) $atk.raw

        $q = VMArgs { param($p) & C:\sar\inject_probe.exe query $p 2>&1 | Out-String } @($bPid)
        Metric "OBGUARD query on exempt victim (FP control)" $q.Trim()
        Assert "T2-OBGUARD query/read of exempt victim STILL WORKS (no usability loss)" ($q -match 'open=1') $q.Trim()
    }

    # ── T2-AUTO: a whitelisted app launched now is auto-exempted WITHOUT any manual verdict ──
    Write-Host "`nPhase T2-AUTO: whitelisted app auto-exempted on launch (no manual verdict step)" -ForegroundColor Yellow
    SetMode "enforce"
    $aPid = LaunchHold 25
    Start-Sleep -Seconds 3
    $apq = VMArgs { param($p) & C:\sar\sarctl.exe procquery $p 2>&1 | Out-String } @($aPid)
    Metric "AUTO procquery (no manual verdict issued for this pid)" $apq.Trim()
    Assert "T2-AUTO whitelisted app is EXEMPT via auto-verdict (no manual step)" ($apq -match 'id_state=2\(exempt\)') "procquery: $($apq.Trim())"
    $aatk = RunAttack $aPid
    Metric "AUTO attack on auto-exempted app" $aatk.raw
    Assert "T2-AUTO injection into auto-exempted app is BLOCKED" ($aatk.write -eq 0) $aatk.raw
    VMArgs { param($p) Stop-Process -Id $p -Force -ErrorAction SilentlyContinue } @($aPid) | Out-Null

    VMArgs { param($p) Stop-Process -Id $p -Force -ErrorAction SilentlyContinue } @($bPid) | Out-Null
    VM { & C:\sar\sarctl.exe whitelist-remove C:\sar\inject_probe.exe 2>&1 | Out-Null } | Out-Null
}

SetMode "audit"

Write-Host ""
Write-Host ("RESULT  pass={0}  fail={1}  skip={2}" -f $pass, $fail, $skip) -ForegroundColor White
if ($fail -gt 0) { exit 1 } else { exit 0 }
