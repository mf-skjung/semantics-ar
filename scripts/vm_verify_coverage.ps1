param(
    [string]$VMName = "SarTarget",
    [string]$Repo = (Join-Path $PSScriptRoot ".."),
    [PSCredential]$Credential,
    [int]$BenignCount = 200,
    [int]$AttackCount = 30,
    [int]$PerfCount = 400
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
    for ($i = 0; $i -lt 6; $i++) {
        try { $script:Sess = New-PSSession -VMName $VMName -Credential $Credential -ErrorAction Stop; return }
        catch { Start-Sleep -Seconds 5 }
    }
    throw "Cannot open PowerShell Direct session to VM '$VMName'. Verify the VM is running, Guest Services/PSDirect is enabled, and the credential is valid."
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
function CopyToVM { param([string]$Local,[string]$Remote)
    Connect-VM
    Copy-Item -Path $Local -Destination $Remote -ToSession $script:Sess -Force
}

$pass = 0; $fail = 0
function Assert { param([string]$Name,[bool]$Cond,[string]$Detail="")
    if ($Cond) { Write-Host "  PASS  $Name" -ForegroundColor Green; $script:pass++ }
    else { Write-Host "  FAIL  $Name  $Detail" -ForegroundColor Red; $script:fail++ }
}
function Metric { param([string]$Name,[string]$Value) Write-Host ("  METRIC  {0,-42} {1}" -f $Name, $Value) -ForegroundColor Cyan }

function PreserveCount {
    VM {
        $out = & C:\sar\sarctl.exe preserve-list 2>&1
        $m = $out | Select-String '(\d+) preserved region'
        if ($m) { [int]$m.Matches[0].Groups[1].Value } else { 0 }
    }
}
function GoldenHash { param([int]$Bytes)
    VMArgs { param($n)
        $b = [byte[]]::new($n)
        for ($i=0;$i -lt $n;$i++){ $b[$i] = [byte](65 + ($i % 26)) }
        $t = [IO.Path]::GetTempFileName(); [IO.File]::WriteAllBytes($t,$b)
        $h = (Get-FileHash $t -Algorithm SHA256).Hash; Remove-Item $t; $h
    } @($Bytes)
}

Write-Host "`n=============================================" -ForegroundColor Cyan
Write-Host " semantics-ar Coverage: non-in-place / FP / FN / burden" -ForegroundColor Cyan
Write-Host " $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Cyan
Write-Host "=============================================`n" -ForegroundColor Cyan

# ── Preflight ──────────────────────────────────────────────────────────
Write-Host "Preflight: VM connectivity" -ForegroundColor Yellow
$vm = Get-VM -Name $VMName -ErrorAction SilentlyContinue
if (-not $vm) { throw "VM '$VMName' not found on this host (Get-VM)." }
if ($vm.State -ne 'Running') { throw "VM '$VMName' is '$($vm.State)', not Running. Start it first." }
Write-Host "  VM '$VMName' State=$($vm.State) Uptime=$($vm.Uptime)"
$ping = VM { "$(hostname)|$($PSVersionTable.PSVersion)|loaded=$([bool]((fltmc filters 2>$null) -match 'semantics_ar'))" }
Write-Host "  VM responds: $ping"
Assert "PowerShell Direct session is stable" ($null -ne $ping)

# ── Deploy ─────────────────────────────────────────────────────────────
Write-Host "Phase 0: Package & Deploy" -ForegroundColor Yellow
if (-not (Test-Path "$Repo\build_driver\semantics_ar.sys")) {
    & cmd /c "`"$Repo\scripts\build_driver.bat`" `"$Repo\build_driver`"" 2>&1 | Out-Null
}
& "$Repo\scripts\package_driver.ps1" -Out "$Repo\build_driver" -Repo $Repo 2>&1 | Out-Null
$pkg = "$Repo\build_driver\pkg"
if (-not (Test-Path "$pkg\semantics_ar.sys")) { throw "Package failed" }

try { VM { Stop-Service semantics_ar_service -Force -ErrorAction SilentlyContinue } } catch {}
try { VM { fltmc unload semantics_ar 2>$null } } catch {}
Start-Sleep -Seconds 2
VM { $s="C:\Windows\System32\drivers\SemanticsAr"; if (Test-Path $s){ Remove-Item "$s\*" -Force -ErrorAction SilentlyContinue } }

$vmSar = "C:\sar"
VM { if (-not (Test-Path $using:vmSar)) { New-Item -ItemType Directory -Path $using:vmSar -Force | Out-Null } }
foreach ($f in @("semantics_ar.sys","semantics_ar.inf","semantics_ar.cat","SemanticsArTest.cer")) {
    $src = Join-Path $pkg $f; if (Test-Path $src) { CopyToVM $src (Join-Path $vmSar $f) }
}
$svcExe = "$Repo\build\service\Release\semantics_ar_service.exe"
if (Test-Path $svcExe) { CopyToVM $svcExe "$vmSar\semantics_ar_service.exe" }
$sarctlExe = "$Repo\build\tools\Release\sarctl.exe"
if (Test-Path $sarctlExe) { CopyToVM $sarctlExe "$vmSar\sarctl.exe" }
foreach ($h in @("noninplace_destroyer.exe","benign_workload.exe","perf_bench.exe",
                 "ransom_sim.exe","preserve_test.exe","partial_encryptor.exe")) {
    $src = "$Repo\build_harness\$h"; if (Test-Path $src) { CopyToVM $src "$vmSar\$h" }
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
Assert "minifilter loaded" ([bool]$loaded) "load output: $loadOut"
if (-not $loaded) {
    Write-Host "  Driver did not attach. Aborting before measurement (metrics would be invalid)." -ForegroundColor Red
    throw "minifilter failed to load/attach"
}
VM {
    $p="C:\sar\semantics_ar_service.exe"
    if (-not (Get-Service semantics_ar_service -ErrorAction SilentlyContinue)) { New-Service -Name semantics_ar_service -BinaryPathName $p -StartupType Manual -ErrorAction SilentlyContinue | Out-Null }
    Start-Service semantics_ar_service -ErrorAction SilentlyContinue; Start-Sleep -Seconds 4
}
$svcRunning = VM { (Get-Service semantics_ar_service -ErrorAction SilentlyContinue).Status -eq 'Running' }
Assert "service running" ([bool]$svcRunning)
VM { & C:\sar\sarctl.exe mode audit 2>&1 | Out-Null; & C:\sar\sarctl.exe budget 604800 10240 2>&1 | Out-Null }

# ── Phase A: non-in-place preservation + recovery (= non-in-place FN) ───
Write-Host "`nPhase A: Non-in-place destruction -> preserve -> restore (deterministic per-victim)" -ForegroundColor Yellow
$sizeKB = 16
$fbytes = $sizeKB * 1024
$modeRates = @{}
foreach ($mode in @("newdelete","renameover","truncate","setzero","mmap")) {
    $dir = "C:\sar\nip_$mode"
    VMArgs { param($d) if (Test-Path $d){Remove-Item $d -Recurse -Force -ErrorAction SilentlyContinue}; New-Item -ItemType Directory $d -Force -ErrorAction SilentlyContinue|Out-Null } @($dir)
    VMArgs { param($m,$d,$n,$k) & C:\sar\noninplace_destroyer.exe $m $d $n $k 2>&1 | Out-Null } @($mode,$dir,$AttackCount,$sizeKB)
    Start-Sleep -Seconds 8
    $off = 0; $len = $fbytes
    if ($mode -eq 'truncate') { $off = [int]($fbytes / 2); $len = [int]($fbytes / 2) }

    $res = VMArgs { param($d,$n,$off,$len)
        $rec = 0; $found = 0; $samples = @()
        for ($i = 0; $i -lt $n; $i++) {
            $v = "{0}\victim_{1:D5}.dat" -f $d, $i
            $rc = ((& C:\sar\sarctl.exe preserve-recover $v $off $len 2>&1) -join ' ').Trim()
            if ($rc -match 'result=0') { $found++ }
            $ok = $false
            if (Test-Path $v) {
                try {
                    $fb = [IO.File]::ReadAllBytes($v)
                    if ($fb.Length -ge ($off + $len)) {
                        $ok = $true
                        for ($k = 0; $k -lt $len; $k++) {
                            if ($fb[$off + $k] -ne [byte](65 + (($off + $k) % 26))) { $ok = $false; break }
                        }
                    }
                } catch { $ok = $false }
            }
            if ($ok) { $rec++ }
            if ($samples.Count -lt 2) { $samples += "v$i rc=[$rc] region_ok=$ok" }
        }
        return [pscustomobject]@{ recovered = $rec; found = $found; samples = $samples }
    } @($dir,$AttackCount,$off,$len)

    $rate = if ($AttackCount -gt 0) { [math]::Round(100.0 * $res.recovered / $AttackCount, 1) } else { 0 }
    $modeRates[$mode] = $rate
    Metric "non-in-place[$mode] recover-accepted (result=0)" "$($res.found)/$AttackCount"
    Metric "non-in-place[$mode] restored-region-correct" "$($res.recovered)/$AttackCount ($rate%)"
    foreach ($s in $res.samples) { Write-Host "      $s" -ForegroundColor DarkGray }
}
$nipHard = @("newdelete","renameover","truncate","setzero")
$hardOk = $true
foreach ($m in $nipHard) { if ($modeRates[$m] -lt 95.0) { $hardOk = $false } }
Assert "non-in-place FN=0 on synchronous paths (>=95% recovered)" $hardOk `
    ("rates: " + (($nipHard | ForEach-Object { "$_=$($modeRates[$_])%" }) -join " "))
Write-Host "  (mmap is best-effort before async flush: $($modeRates['mmap'])% recovered)" -ForegroundColor DarkGray

# ── Phase E: in-place encryption -> preserve -> restore (in-place FN) ───
Write-Host "`nPhase E: In-place encryption -> preserve -> restore (deterministic per-victim)" -ForegroundColor Yellow
VM { & C:\sar\sarctl.exe mode audit 2>&1 | Out-Null; & C:\sar\sarctl.exe budget 604800 10240 2>&1 | Out-Null }
$ipDir = "C:\sar\inplace"
$ipLen = 16384
VMArgs { param($d,$n,$len)
    function WriteFlush { param($p,$bytes)
        $fs=[IO.File]::Open($p,'Create','Write','None'); $fs.Write($bytes,0,$bytes.Length); $fs.Flush($true); $fs.Close()
    }
    if (Test-Path $d){Remove-Item $d -Recurse -Force -ErrorAction SilentlyContinue}
    New-Item -ItemType Directory $d -Force -ErrorAction SilentlyContinue | Out-Null
    for ($i=0;$i -lt $n;$i++){
        $b=[byte[]]::new($len)
        for($k=0;$k -lt $len;$k++){ $b[$k]=[byte](65 + ($k % 26)) }
        WriteFlush ("{0}\victim_{1:D5}.dat" -f $d,$i) $b
    }
    $w=[byte[]]::new($len); for($k=0;$k -lt $len;$k++){ $w[$k]=[byte](88) }
    WriteFlush "$d\warmup.dat" $w
} @($ipDir,$AttackCount,$ipLen)
Start-Sleep -Seconds 3
VM { & C:\sar\preserve_test.exe C:\sar\inplace\warmup.dat 2>&1 | Out-Null }
Start-Sleep -Seconds 3
VMArgs { param($d,$n)
    $files=@(); for($i=0;$i -lt $n;$i++){ $files += ("{0}\victim_{1:D5}.dat" -f $d,$i) }
    & C:\sar\preserve_test.exe @files 2>&1 | Out-Null
} @($ipDir,$AttackCount)
Start-Sleep -Seconds 10
$ipDiag = VMArgs { param($d,$n)
    $ks = (& C:\sar\sarctl.exe list 2>&1) -join "`n"
    $pl = (& C:\sar\sarctl.exe preserve-list 2>&1) -join "`n"
    $convicted=0; $held=0
    for($i=0;$i -lt $n;$i++){ $vn="victim_{0:D5}" -f $i; if($ks -match $vn){$convicted++}; if($pl -match $vn){$held++} }
    [pscustomobject]@{convicted=$convicted;held=$held}
} @($ipDir,$AttackCount)
Metric "in-place victims: preserved-hold / oracle-keyed" "$($ipDiag.held) / $($ipDiag.convicted)"
$ipRes = VMArgs { param($d,$n,$off,$len)
    function FirstBad { param($p,$o,$l)
        if (-not (Test-Path $p)) { return -2 }
        try { $fb = [IO.File]::ReadAllBytes($p) } catch { return -3 }
        if ($fb.Length -lt ($o + $l)) { return -4 }
        for ($k=0;$k -lt $l;$k++){ if ($fb[$o+$k] -ne [byte](65 + (($o+$k) % 26))){ return $k } }
        return -1
    }
    $rec = 0; $found = 0; $viaKey = 0; $samples = @(); $badoffs = @()
    $ks = & C:\sar\sarctl.exe list 2>&1
    for ($i = 0; $i -lt $n; $i++) {
        $vn = "victim_{0:D5}.dat" -f $i
        $v = "$d\$vn"
        $rc = ((& C:\sar\sarctl.exe preserve-recover $v $off $len 2>&1) -join ' ').Trim()
        if ($rc -match 'result=0') { $found++ }
        $fb = FirstBad $v $off $len
        $ok = ($fb -eq -1)
        $how = 'preserve'
        if (-not $ok) {
            if ($badoffs.Count -lt 6) { $badoffs += "v$i firstBad=$fb" }
            $keyid = $null
            foreach ($line in $ks) {
                if ($line -match [regex]::Escape($vn) -and $line -match 'key_id=([0-9a-f]+)') { $keyid = $Matches[1]; break }
            }
            if ($keyid) {
                & C:\sar\sarctl.exe recover $keyid $v 2>&1 | Out-Null
                $ok = ((FirstBad $v $off $len) -eq -1)
                if ($ok) { $how = 'key' }
            }
        }
        if ($ok) { $rec++; if ($how -eq 'key') { $viaKey++ } }
        if ($samples.Count -lt 3) { $samples += "v$i rc=[$rc] ok=$ok via=$(if($ok){$how}else{'none'})" }
    }
    return [pscustomobject]@{ recovered = $rec; found = $found; viaKey = $viaKey; samples = $samples; badoffs = $badoffs }
} @($ipDir,$AttackCount,0,$ipLen)
$ipRate = if ($AttackCount -gt 0) { [math]::Round(100.0 * $ipRes.recovered / $AttackCount, 1) } else { 0 }
Metric "in-place recover-accepted (preserve result=0)" "$($ipRes.found)/$AttackCount"
Metric "in-place restored-region-correct (any path)" "$($ipRes.recovered)/$AttackCount ($ipRate%)"
Metric "  of which via captured-key fallback" "$($ipRes.viaKey)"
foreach ($s in $ipRes.samples) { Write-Host "      $s" -ForegroundColor DarkGray }
if ($ipRes.badoffs.Count -gt 0) { Write-Host "      tear offsets: $($ipRes.badoffs -join '  ')" -ForegroundColor DarkYellow }
Assert "in-place encryption FN=0 (>=95% recovered, preserve or key)" ($ipRate -ge 95.0) "rate=$ipRate%"

# ── Phase F: Oracle key-capture -> forward-proof -> reconcile ──────────
Write-Host "`nPhase F: Oracle key-capture (reused key) -> recover -> reconcile" -ForegroundColor Yellow
$oDir = "C:\sar\oracle"
$oCount = 3
$oLen = 8192
VMArgs { param($d,$n,$len)
    function WriteFlush { param($p,$bytes)
        $fs=[IO.File]::Open($p,'Create','Write','None'); $fs.Write($bytes,0,$bytes.Length); $fs.Flush($true); $fs.Close()
    }
    if (Test-Path $d){Remove-Item $d -Recurse -Force -ErrorAction SilentlyContinue}
    New-Item -ItemType Directory $d -Force -ErrorAction SilentlyContinue | Out-Null
    for ($i=0;$i -lt $n;$i++){
        $b=[byte[]]::new($len)
        for($k=0;$k -lt $len;$k++){ $b[$k]=[byte](65 + ($k % 26)) }
        WriteFlush ("{0}\ovictim_{1:D5}.dat" -f $d,$i) $b
    }
} @($oDir,$oCount,$oLen)
VMArgs { param($d,$n)
    for ($i=0;$i -lt $n;$i++){
        Start-Process -FilePath C:\sar\partial_encryptor.exe -ArgumentList (("{0}\ovictim_{1:D5}.dat" -f $d,$i),"8") -NoNewWindow
    }
} @($oDir,$oCount)
Write-Host "  partial_encryptor running (reused fixed key, 12s hold) — waiting for Oracle conviction..."
Start-Sleep -Seconds 22
$oRes = VMArgs { param($d,$n,$len)
    $klist = & C:\sar\sarctl.exe list 2>&1
    $captured = 0; $recovered = 0; $samples = @()
    for ($i = 0; $i -lt $n; $i++) {
        $vname = "ovictim_{0:D5}.dat" -f $i
        $v = "$d\$vname"
        $keyid = $null
        foreach ($line in $klist) {
            if ($line -match [regex]::Escape($vname) -and $line -match 'key_id=([0-9a-f]+)') { $keyid = $Matches[1]; break }
        }
        $rc = ''
        if ($keyid) { $captured++; $rc = ((& C:\sar\sarctl.exe recover $keyid $v 2>&1) -join ' ').Trim() }
        $ok = $false
        if (Test-Path $v) {
            try {
                $fb = [IO.File]::ReadAllBytes($v)
                if ($fb.Length -ge $len) {
                    $ok = $true
                    for ($k = 0; $k -lt $len; $k++) {
                        if ($fb[$k] -ne [byte](65 + ($k % 26))) { $ok = $false; break }
                    }
                }
            } catch { $ok = $false }
        }
        if ($ok) { $recovered++ }
        if ($samples.Count -lt 2) { $samples += "ov$i key_captured=$([bool]$keyid) rc=[$rc] region_ok=$ok" }
    }
    return [pscustomobject]@{ captured = $captured; recovered = $recovered; samples = $samples }
} @($oDir,$oCount,$oLen)
$oReconStill = VMArgs { param($d,$n)
    $pl = (& C:\sar\sarctl.exe preserve-list 2>&1) -join "`n"
    $still = 0; for ($i=0;$i -lt $n;$i++){ if ($pl -match ("ovictim_{0:D5}" -f $i)){ $still++ } }
    $still
} @($oDir,$oCount)
Metric "Oracle keys captured (reused-key forward-proof)" "$($oRes.captured)/$oCount"
Metric "Oracle recover restored-region-correct" "$($oRes.recovered)/$oCount"
Metric "Oracle reconcile: convicted files still held in preserve" "$oReconStill/$oCount"
foreach ($s in $oRes.samples) { Write-Host "      $s" -ForegroundColor DarkGray }
Assert "Oracle captures reused key (forward-proof, all victims)" ($oRes.captured -eq $oCount) "captured=$($oRes.captured)/$oCount"
Assert "Oracle decrypts forward to golden (all victims)" ($oRes.recovered -eq $oCount) "recovered=$($oRes.recovered)/$oCount"

# ── Phase B: FP / availability under AUDIT ─────────────────────────────
Write-Host "`nPhase B: Benign workload under AUDIT (FP load + availability)" -ForegroundColor Yellow
VM { & C:\sar\sarctl.exe mode audit 2>&1 | Out-Null }
$benDir = "C:\sar\benign_audit"
VMArgs { param($d) if (Test-Path $d){Remove-Item $d -Recurse -Force -ErrorAction SilentlyContinue}; New-Item -ItemType Directory $d -Force -ErrorAction SilentlyContinue|Out-Null } @($benDir)
$benOut = VMArgs { param($d,$n) & C:\sar\benign_workload.exe $d $n 32 2>&1 } @($benDir,$BenignCount)
foreach ($l in $benOut) { Write-Host "    $l" }
Start-Sleep -Seconds 6
$benBlocked = 0
$bl = $benOut | Select-String 'BLOCKED_TOTAL=(\d+)'; if ($bl) { $benBlocked = [int]$bl.Matches[0].Groups[1].Value }
$benHolds = VM {
    $list = & C:\sar\sarctl.exe preserve-list 2>&1
    ($list | Where-Object { $_ -match 'benign_audit' }).Count
}
Metric "AUDIT benign probation holds (per $BenignCount docs, dir-matched)" "$benHolds"
Metric "AUDIT benign blocked operations" "$benBlocked"
Assert "AUDIT never blocks benign work" ($benBlocked -eq 0)

# ── Phase C: FP / availability under ENFORCE (default budget) ──────────
Write-Host "`nPhase C: Benign workload under ENFORCE (default 10GB budget)" -ForegroundColor Yellow
VM { & C:\sar\sarctl.exe mode enforce 2>&1 | Out-Null; & C:\sar\sarctl.exe budget 604800 10240 2>&1 | Out-Null }
$benDir2 = "C:\sar\benign_enforce"
VMArgs { param($d) if (Test-Path $d){Remove-Item $d -Recurse -Force -ErrorAction SilentlyContinue}; New-Item -ItemType Directory $d -Force -ErrorAction SilentlyContinue|Out-Null } @($benDir2)
$benOut2 = VMArgs { param($d,$n) & C:\sar\benign_workload.exe $d $n 32 2>&1 } @($benDir2,$BenignCount)
foreach ($l in $benOut2) { Write-Host "    $l" }
$benBlocked2 = 0
$bl2 = $benOut2 | Select-String 'BLOCKED_TOTAL=(\d+)'; if ($bl2) { $benBlocked2 = [int]$bl2.Matches[0].Groups[1].Value }
Metric "ENFORCE benign blocked operations (default budget)" "$benBlocked2"
Assert "ENFORCE does not block normal usage at default budget" ($benBlocked2 -eq 0)
VM { & C:\sar\sarctl.exe mode audit 2>&1 | Out-Null }

# ── Phase G: ENFORCE capacity exhaustion -> fail-closed block ──────────
Write-Host "`nPhase G: ENFORCE capacity exhaustion -> destruction blocked (fail-closed)" -ForegroundColor Yellow
$ovfDir = "C:\sar\enforce_ovf"
$ovfN = 20; $ovfSz = 131072
VMArgs { param($d,$n,$sz)
    if (Test-Path $d){Remove-Item $d -Recurse -Force -ErrorAction SilentlyContinue}
    New-Item -ItemType Directory $d -Force -ErrorAction SilentlyContinue | Out-Null
    for ($i=0;$i -lt $n;$i++){
        $b=[byte[]]::new($sz)
        for($k=0;$k -lt $sz;$k++){ $b[$k]=[byte](($i*17 + $k) % 256) }
        [IO.File]::WriteAllBytes(("{0}\ovf_{1:D3}.dat" -f $d,$i),$b)
    }
} @($ovfDir,$ovfN,$ovfSz)
Start-Sleep -Seconds 3
VM { & C:\sar\sarctl.exe budget 86400 1 2>&1 | Out-Null; & C:\sar\sarctl.exe mode enforce 2>&1 | Out-Null }
VMArgs { param($d,$n)
    $files=@(); for($i=0;$i -lt $n;$i++){ $files += ("{0}\ovf_{1:D3}.dat" -f $d,$i) }
    & C:\sar\preserve_test.exe @files 2>&1 | Out-Null
} @($ovfDir,$ovfN)
Start-Sleep -Seconds 6
$ovfBlocked = VMArgs { param($d,$n,$sz)
    $blocked = 0
    for ($i=0;$i -lt $n;$i++){
        $f = "{0}\ovf_{1:D3}.dat" -f $d,$i
        $same = $false
        try {
            $cur = [IO.File]::ReadAllBytes($f)
            if ($cur.Length -ge $sz) {
                $same = $true
                for ($k=0;$k -lt $sz;$k++){ if ($cur[$k] -ne [byte](($i*17 + $k) % 256)){ $same=$false; break } }
            }
        } catch {}
        if ($same) { $blocked++ }
    }
    $blocked
} @($ovfDir,$ovfN,$ovfSz)
VM { & C:\sar\sarctl.exe mode audit 2>&1 | Out-Null; & C:\sar\sarctl.exe budget 604800 10240 2>&1 | Out-Null }
Metric "ENFORCE destruction blocked at exhausted capacity (1MB cap)" "$ovfBlocked/$ovfN"
Assert "ENFORCE blocks destruction when capacity exhausted (fail-closed)" ($ovfBlocked -gt 0) "blocked=$ovfBlocked/$ovfN"

# ── Phase H: reboot persistence of staged regions (new record struct) ──
# Runs BEFORE the burden phase so the driver/keystore are in their healthy
# post-deploy state (Phase D's unload/reload cycles would otherwise leave the
# keystore not-ready). A soft reboot may land the VM session in
# ConstrainedLanguage, so every post-reboot check uses only native tools
# (sarctl, certutil) and string ops.
Write-Host "`nPhase H: Reboot persistence of staged regions (new record struct)" -ForegroundColor Yellow
# Clean, isolated store so the reboot test is not confounded by A-G eviction state.
VM { Stop-Service semantics_ar_service -Force -ErrorAction SilentlyContinue }
VM { fltmc unload semantics_ar 2>$null | Out-Null }
Start-Sleep -Seconds 2
VM { $s="C:\Windows\System32\drivers\SemanticsAr"; if (Test-Path $s){ Remove-Item "$s\*" -Force -ErrorAction SilentlyContinue } }
VM { fltmc load semantics_ar 2>$null | Out-Null }
Start-Sleep -Seconds 3
VM { Start-Service semantics_ar_service -ErrorAction SilentlyContinue; Start-Sleep -Seconds 4 }
VM { & C:\sar\sarctl.exe mode audit 2>&1 | Out-Null; & C:\sar\sarctl.exe budget 604800 10240 2>&1 | Out-Null }
$pDir = "C:\sar\persist"; $pLen = 16384
$pGolden = VMArgs { param($d,$len)
    if (Test-Path $d){Remove-Item $d -Recurse -Force -ErrorAction SilentlyContinue}
    New-Item -ItemType Directory $d -Force -ErrorAction SilentlyContinue | Out-Null
    $b=[byte[]]::new($len); for($k=0;$k -lt $len;$k++){ $b[$k]=[byte](65 + ($k % 26)) }
    $fs=[IO.File]::Open("$d\pvictim.dat",'Create','Write','None'); $fs.Write($b,0,$len); $fs.Flush($true); $fs.Close()
    (Get-FileHash "$d\pvictim.dat" -Algorithm SHA256).Hash
} @($pDir,$pLen)
Start-Sleep -Seconds 2
VM { & C:\sar\preserve_test.exe C:\sar\persist\pvictim.dat 2>&1 | Out-Null }
$staged = $false
for ($t=0; $t -lt 15; $t++) {
    Start-Sleep -Seconds 2
    $staged = VM { ((& C:\sar\sarctl.exe preserve-list 2>&1) -join "`n") -match 'pvictim' }
    if ($staged) { break }
}
$preList = VM { & C:\sar\sarctl.exe preserve-list 2>&1 }
$preCount = 0; $mp = $preList | Select-String '(\d+) preserved region'; if ($mp){ $preCount=[int]$mp.Matches[0].Groups[1].Value }
Metric "persist victim staged pre-reboot" "$staged (store entries=$preCount)"
Assert "persist victim staged before reboot" ([bool]$staged)
Write-Host "  Waiting for persist-thread flush (10s), then soft-rebooting..." -ForegroundColor DarkGray
Start-Sleep -Seconds 10

try { VM { shutdown /r /t 0 /f } } catch {}
$script:Sess = $null
Start-Sleep -Seconds 25
for ($t=0; $t -lt 48; $t++) { Start-Sleep -Seconds 5; try { Connect-VM; break } catch {} }
Start-Sleep -Seconds 5
$langMode = VM { "$($ExecutionContext.SessionState.LanguageMode)" }
Write-Host "  Post-reboot LanguageMode = $langMode (native-only checks follow)" -ForegroundColor DarkGray
$diskState = VM {
    $d="C:\Windows\System32\drivers\SemanticsAr"
    ((Get-ChildItem $d -ErrorAction SilentlyContinue | ForEach-Object { "$($_.Name)=$($_.Length)" }) -join ', ')
}
Write-Host "  post-reboot store files (pre-load): $diskState" -ForegroundColor DarkGray
VM { fltmc load semantics_ar 2>$null | Out-Null }
Start-Sleep -Seconds 3
$postLoadOnly = VM {
    $m = (& C:\sar\sarctl.exe preserve-list 2>&1) | Select-String '(\d+) preserved region'
    if ($m) { $m.Matches[0].Groups[1].Value } else { 'no-list' }
}
Write-Host "  post-reboot entries after load, BEFORE service start: $postLoadOnly" -ForegroundColor DarkGray
VM { Start-Service semantics_ar_service -ErrorAction SilentlyContinue; Start-Sleep -Seconds 3 }
$postLoaded = VM { [bool]((fltmc filters 2>$null) -match 'semantics_ar') }
Assert "minifilter loaded after reboot" ([bool]$postLoaded)

$postList = VM { & C:\sar\sarctl.exe preserve-list 2>&1 }
$postCount = 0; $mp2 = $postList | Select-String '(\d+) preserved region'; if ($mp2){ $postCount=[int]$mp2.Matches[0].Groups[1].Value }
$survived = ($postList -join "`n") -match 'pvictim'
Metric "preserve entries pre/post reboot" "$preCount -> $postCount"
Metric "pvictim present in post-reboot list (informational)" "$survived"

$recRc = VM { ((& C:\sar\sarctl.exe preserve-recover C:\sar\persist\pvictim.dat 0 16384 2>&1) -join ' ').Trim() }
Assert "staged region survived reboot (recover accepted)" ($recRc -match 'result=0') "rc=$recRc"
$postHash = VM {
    $o = certutil -hashfile C:\sar\persist\pvictim.dat SHA256 2>&1
    (($o | Where-Object { $_ -match '^[0-9a-fA-F][0-9a-fA-F ]{38,}$' } | Select-Object -First 1) -replace '\s','')
}
Metric "post-reboot recover result" "$recRc"
Assert "recovered region byte-matches golden after reboot" ([bool]($postHash -and ($postHash -eq $pGolden))) "post=$postHash golden=$pGolden"

# ── Phase D: Burden (clean-store, apples-to-apples, median of 3) ───────
# Runs LAST: its driver unload/reload cycles are destructive to keystore
# readiness, so nothing depends on driver state after this. Passive overhead
# on benign file ops (perf_bench writes are not encryption-novel, so the Gate
# never fires and no COW staging occurs). Baseline is driver-unloaded; each
# loaded run starts from a freshly loaded EMPTY store. CLM-safe: no .NET
# method calls inside VM scriptblocks (post-reboot session may be CLM).
Write-Host "`nPhase D: Burden (passive overhead, benign file ops; clean store, median of 3)" -ForegroundColor Yellow
$perfDir = "C:\sar\perf"
$storeClr = { $s="C:\Windows\System32\drivers\SemanticsAr"; if (Test-Path $s){ Remove-Item "$s\*" -Force -ErrorAction SilentlyContinue } }
VMArgs { param($d) if (Test-Path $d){Remove-Item $d -Recurse -Force -ErrorAction SilentlyContinue}; New-Item -ItemType Directory $d -Force -ErrorAction SilentlyContinue|Out-Null } @($perfDir)
$perfOne = { param($d,$n)
    Remove-Item "$d\*" -Force -ErrorAction SilentlyContinue
    $o = & C:\sar\perf_bench.exe $d $n 64 2>&1
    $m = $o | Select-String 'PERF_TOTAL_MS=([\d.]+)'; if ($m){[double]$m.Matches[0].Groups[1].Value}else{-1}
}

VM { fltmc unload semantics_ar 2>$null | Out-Null }
Start-Sleep -Seconds 2
$baseRuns = @()
for ($r = 0; $r -lt 3; $r++) { $baseRuns += (VMArgs $perfOne @($perfDir,$PerfCount)) }
$baseMs = ([double[]]($baseRuns | Sort-Object))[1]

$loadRuns = @()
for ($r = 0; $r -lt 3; $r++) {
    VM { fltmc unload semantics_ar 2>$null | Out-Null }
    Start-Sleep -Seconds 2
    VM $storeClr
    VM { fltmc load semantics_ar 2>$null | Out-Null }
    Start-Sleep -Seconds 3
    VM { & C:\sar\sarctl.exe mode audit 2>&1 | Out-Null; & C:\sar\sarctl.exe budget 604800 10240 2>&1 | Out-Null }
    $loadRuns += (VMArgs $perfOne @($perfDir,$PerfCount))
}
$loadedMs = ([double[]]($loadRuns | Sort-Object))[1]

$overhead = if ($baseMs -gt 0) { [math]::Round(100.0 * ($loadedMs - $baseMs) / $baseMs, 1) } else { -1 }
Metric "perf baseline median (unloaded, ms)" ("{0}  [{1}]" -f $baseMs, (($baseRuns | ForEach-Object { [math]::Round($_,0) }) -join ', '))
Metric "perf loaded median (clean store, ms)" ("{0}  [{1}]" -f $loadedMs, (($loadRuns | ForEach-Object { [math]::Round($_,0) }) -join ', '))
Metric "file-op overhead vs baseline (passive, benign ops)" "$overhead%"
$storeBytes = VM {
    $d="C:\Windows\System32\drivers\SemanticsAr"
    if (Test-Path $d) { (Get-ChildItem $d -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum } else { 0 }
}
$storeMB = if ($storeBytes) { [math]::Round($storeBytes/1MB,2) } else { 0 }
Metric "preserve store on-disk size after 1 cold run (MB)" "$storeMB"

# ── Summary ────────────────────────────────────────────────────────────
Write-Host "`n=============================================" -ForegroundColor Cyan
Write-Host " RESULTS: $pass passed, $fail failed" -ForegroundColor $(if ($fail -eq 0){'Green'}else{'Red'})
Write-Host "=============================================`n" -ForegroundColor Cyan
if ($fail -gt 0) { exit 1 }
