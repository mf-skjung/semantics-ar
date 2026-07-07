param(
    [string]$VMName = "SarTarget",
    [string]$Repo = (Join-Path $PSScriptRoot ".."),
    [PSCredential]$Credential,
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
    for ($i = 0; $i -lt 6; $i++) {
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

$pass = 0; $fail = 0
function Assert { param([string]$Name,[bool]$Cond,[string]$Detail="")
    if ($Cond) { Write-Host "  PASS  $Name" -ForegroundColor Green; $script:pass++ }
    else { Write-Host "  FAIL  $Name  $Detail" -ForegroundColor Red; $script:fail++ }
}
function Metric { param([string]$Name,[string]$Value) Write-Host ("  METRIC  {0,-46} {1}" -f $Name, $Value) -ForegroundColor Cyan }

function CapturedCount { VM { $o = & C:\sar\sarctl.exe list 2>&1; $m = $o | Select-String '(\d+) captured key'; if ($m) { [int]$m.Matches[0].Groups[1].Value } else { 0 } } }
function EventClassCount { param([string]$Cls,[int]$N=64) VMArgs { param($c,$n) $o = & C:\sar\sarctl.exe events $n 2>&1; ($o | Select-String ("class=" + $c)).Count } @($Cls,$N) }

# Barrier: the transform process has exited and the off-IRP capture/convict pipeline has drained
# (captured-count stable across two reads). Replaces fixed sleeps; also the FABLE5 in-flight-TOCTOU fix.
function DrainBarrier { param([int]$MaxSec=40)
    Start-Sleep -Seconds 4
    $prev = -1; $stable = 0; $t = 0
    while ($t -lt $MaxSec) {
        $c = CapturedCount
        if ($c -eq $prev) { $stable++; if ($stable -ge 3) { return $c } } else { $stable = 0 }
        $prev = $c; Start-Sleep -Seconds 2; $t += 2
    }
    return $prev
}

# Captured keys propagate to the queryable keystore asynchronously (the service pulls them off the
# driver), so a count-stability barrier is not enough for a capture-expecting phase. Poll the control
# port until this specific corpus's key record (unique dir-leaf) is visible, or give up after MaxSec.
function WaitForKey { param([string]$Dir,[int]$MaxSec=60)
    $leaf = Split-Path $Dir -Leaf
    $t = 0
    while ($t -lt $MaxSec) {
        $found = VMArgs { param($l) (& C:\sar\sarctl.exe list 2>&1 | Select-String ([regex]::Escape($l)) | Select-String 'key_id=').Count } @($leaf)
        if ($found -gt 0) { Start-Sleep -Seconds 2; return $true }
        Start-Sleep -Seconds 3; $t += 3
    }
    return $false
}

# Provenance-bound key lookup (FABLE5 identity-binding fix): key_id whose keystore record path IS this file.
function KeyIdFor { param([string]$FileName,[object]$KList)
    foreach ($line in $KList) {
        if ($line -match [regex]::Escape($FileName) -and $line -match 'key_id=([0-9a-f]+)') { return $Matches[1] }
    }
    return $null
}
function PreserveRegionFor { param([string]$FileName,[object]$PList)
    foreach ($line in $PList) {
        if ($line -match [regex]::Escape($FileName) -and $line -match 'off=(\d+) len=(\d+)') { return @([uint64]$Matches[1],[uint64]$Matches[2]) }
    }
    return $null
}

Write-Host "`n=== semantics-ar stream + fail-closed verification ($(Get-Date -Format 'HH:mm:ss')) ===" -ForegroundColor Cyan

# ── Deploy (pre-signed package; package_driver.ps1 is not used) ──────────
if (-not $SkipDeploy) {
    Write-Host "Deploy: pre-signed package" -ForegroundColor Yellow
    $pkg = "$Repo\build_driver\pkg"
    foreach ($f in @("semantics_ar.sys","semantics_ar.cat","semantics_ar.inf")) {
        if (-not (Test-Path "$pkg\$f")) { throw "pre-signed package missing $f (build+sign first)" }
    }
    try { VM { Stop-Service semantics_ar_service -Force -ErrorAction SilentlyContinue } } catch {}
    try { VM { fltmc unload semantics_ar 2>$null } } catch {}
    Start-Sleep -Seconds 2
    VM { $s="C:\Windows\System32\drivers\SemanticsAr"; if (Test-Path $s){ Remove-Item "$s\*" -Recurse -Force -ErrorAction SilentlyContinue } }
    VM { if (-not (Test-Path "C:\sar")) { New-Item -ItemType Directory -Path "C:\sar" -Force | Out-Null } }
    foreach ($f in @("semantics_ar.sys","semantics_ar.inf","semantics_ar.cat","SemanticsArTest.cer")) {
        $src = Join-Path $pkg $f; if (Test-Path $src) { CopyToVM $src (Join-Path "C:\sar" $f) }
    }
    $svcExe = "$Repo\build\service\Release\semantics_ar_service.exe"; if (Test-Path $svcExe) { CopyToVM $svcExe "C:\sar\semantics_ar_service.exe" }
    $sarctlExe = "$Repo\build\tools\Release\sarctl.exe"; if (Test-Path $sarctlExe) { CopyToVM $sarctlExe "C:\sar\sarctl.exe" }
    foreach ($h in @("stream_transform.exe","preserve_test.exe","destroyer_matrix.exe")) {
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
    Assert "minifilter loaded" ([bool]$loaded) "load: $loadOut"
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

function MakeCorpus { param([string]$Dir,[int]$N,[int]$Len)
    VMArgs { param($d,$n,$len)
        if (Test-Path $d){Remove-Item $d -Recurse -Force -ErrorAction SilentlyContinue}
        New-Item -ItemType Directory $d -Force | Out-Null
        for ($i=0;$i -lt $n;$i++){
            $b=[byte[]]::new($len); for($k=0;$k -lt $len;$k++){ $b[$k]=[byte](65 + ($k % 26)) }
            [IO.File]::WriteAllBytes(("{0}\sv_{1:D5}.dat" -f $d,$i),$b)
        }
    } @($Dir,$N,$Len)
}
# Recover a corpus and classify each file: RECOVERED_BY_KEY / RECOVERED_BY_PRESERVE / REFUSED / LOST.
# Recovery is provenance-bound (key_id/region whose record path is that file); verification re-reads the
# durable file (Get-FileHash) and compares to the golden pattern's hash.
function ClassifyCorpus { param([string]$Dir,[int]$N,[int]$Len,[switch]$AllowRefused)
    VMArgs { param($d,$n,$len,$allowRefused)
        $gb=[byte[]]::new($len); for($k=0;$k -lt $len;$k++){ $gb[$k]=[byte](65+($k%26)) }
        $gt=[IO.Path]::GetTempFileName(); [IO.File]::WriteAllBytes($gt,$gb)
        $gh=(Get-FileHash $gt -Algorithm SHA256).Hash; Remove-Item $gt -Force
        $isGolden = { param($p) if (-not (Test-Path $p)) { return $false }; return (Get-FileHash $p -Algorithm SHA256).Hash -eq $gh }
        # provenance is matched by (unique dir-leaf + filename): every phase reuses sv_%05d.dat, so a bare
        # filename match would bind a file to another dir's key/region (FABLE5 identity-binding hole #4).
        $leaf = Split-Path $d -Leaf
        $klist = & C:\sar\sarctl.exe list 2>&1
        $plist = & C:\sar\sarctl.exe preserve-list 2>&1
        $byKey=0; $byPre=0; $refused=0; $lost=0; $samples=@()
        for ($i=0;$i -lt $n;$i++){
            $vn = "sv_{0:D5}.dat" -f $i; $v = "$d\$vn"
            if ((& $isGolden $v)) {
                if ($allowRefused) { $refused++ } else { $byPre++ }
                continue
            }
            $cls=$null; $kid=$null
            foreach ($line in $klist) { if ($line -match [regex]::Escape($leaf) -and $line -match [regex]::Escape($vn) -and $line -match 'key_id=([0-9a-f]+)') { $kid=$Matches[1]; break } }
            if ($kid) { & C:\sar\sarctl.exe recover $kid $v 2>&1 | Out-Null; if ((& $isGolden $v)) { $byKey++; $cls='key' } }
            if (-not $cls) {
                $off=$null;$ln=$null
                foreach ($line in $plist) { if ($line -match [regex]::Escape($leaf) -and $line -match [regex]::Escape($vn) -and $line -match 'off=(\d+) len=(\d+)') { $off=$Matches[1];$ln=$Matches[2]; break } }
                if ($off -ne $null) { & C:\sar\sarctl.exe preserve-recover $v $off $ln 2>&1 | Out-Null; if ((& $isGolden $v)) { $byPre++; $cls='pre' } }
            }
            if (-not $cls) { $lost++; if ($samples.Count -lt 3){ $samples += $vn } }
        }
        [pscustomobject]@{ byKey=$byKey; byPre=$byPre; refused=$refused; lost=$lost; total=$n; samples=$samples }
    } @($Dir,$N,$Len,[bool]$AllowRefused)
}

# Ordering is load-bearing. The conviction/residency phases (S, F2, S-res) run FIRST, on the pristine
# post-deploy driver: the once-per-process deferred snapshot and the preserve store are both clean, so
# sigma-scan capture is measured without interference. Phase G runs LAST, because its ENFORCE tiny-budget
# capacity exhaustion pushes the preserve store into its exhausted state -- running it earlier would
# poison the snapshot/preserve path for every phase after it. The AUDIT+large-budget phases above emit no
# block-capacity events, so after Phase G the block-capacity count is attributable to Phase G alone.

# Conviction/residency phases: AUDIT + a large budget so no forward-block or capacity-block interferes
# with measuring sigma-scan key capture. The deferred snapshot is once-per-process, so under a reused
# key only one file gets a keystore record; the load-bearing invariants are (a) sigma-scan captures a
# key on a live snapshot (byKey >= 1) and (b) FN=0 (no file lost -- siblings fall to preserve).
VM { & C:\sar\sarctl.exe mode audit 2>&1 | Out-Null; & C:\sar\sarctl.exe budget 604800 10240 2>&1 | Out-Null }

# ── Phase S: stream sigma-scan runtime key capture (verifies engine 2.1) ──
Write-Host "`nPhase S: stream sigma-scan -> recover BY KEY on a live snapshot (AUDIT)" -ForegroundColor Yellow
foreach ($algo in @("chacha","salsa")) {
    $dir = "C:\sar\stream_$algo"; $N=6; $Len=8192
    MakeCorpus $dir $N $Len
    VMArgs { param($a,$d,$n) Start-Process -FilePath C:\sar\stream_transform.exe -ArgumentList @($a,"20","resident",$d,"$n","8") -NoNewWindow -Wait } @($algo,$dir,$N)
    WaitForKey $dir | Out-Null
    $r = ClassifyCorpus $dir $N $Len
    Metric "$algo/20 recovered-by-key / by-preserve" "$($r.byKey) / $($r.byPre) (of $N)"
    Assert "$algo/20 sigma-scan captures a key on a live snapshot" ($r.byKey -ge 1) "byKey=$($r.byKey)"
    Assert "$algo/20 loses zero files (FN=0)" ($r.lost -eq 0) "lost=$($r.lost)"
}

# ── Phase F2: reduced-round Salsa20/12 recovered BY KEY (F0 fix in-kernel) ──
Write-Host "`nPhase F2: Salsa20/12 reduced-round -> recover BY KEY (round-honest)" -ForegroundColor Yellow
$dir="C:\sar\stream_salsa12"; $N=6; $Len=8192
MakeCorpus $dir $N $Len
VMArgs { param($d,$n) Start-Process -FilePath C:\sar\stream_transform.exe -ArgumentList @("salsa","12","resident",$d,"$n","8") -NoNewWindow -Wait } @($dir,$N)
WaitForKey $dir | Out-Null
$r = ClassifyCorpus $dir $N $Len
Metric "Salsa20/12 recovered-by-key" "$($r.byKey)/$N"
Assert "Salsa20/12 convicted and recovered BY KEY (round-honest F0)" ($r.byKey -ge 1 -and $r.lost -eq 0) "byKey=$($r.byKey) byPre=$($r.byPre) lost=$($r.lost)"

# ── Phase S-res: residency hit-rate, per-process, distinct-keyed ─────────
Write-Host "`nPhase S-res: sigma residency (per-process) + oneshot -> preserve" -ForegroundColor Yellow
$M=6
foreach ($resmode in @("resident","oneshot")) {
    $hit=0; $lostTotal=0; $preTotal=0
    for ($p=0;$p -lt $M;$p++){
        $dir="C:\sar\res_${resmode}_$p"; $N=2; $Len=4096
        MakeCorpus $dir $N $Len
        VMArgs { param($rm,$d,$n) Start-Process -FilePath C:\sar\stream_transform.exe -ArgumentList @("chacha","20",$rm,$d,"$n","5") -NoNewWindow -Wait } @($resmode,$dir,$N)
        if ($resmode -eq "resident") { WaitForKey $dir | Out-Null } else { DrainBarrier | Out-Null }
        $r = ClassifyCorpus $dir $N $Len
        if ($r.byKey -gt 0) { $hit++ }
        $preTotal += $r.byPre; $lostTotal += $r.lost
    }
    Metric "$resmode sigma hit-rate (per-process)" "$hit/$M processes"
    if ($resmode -eq "oneshot") {
        Assert "oneshot (sigma-miss) files remain recoverable by PRESERVE (FN=0)" ($lostTotal -eq 0) "lost=$lostTotal preRecovered=$preTotal"
    }
}

# ── Phase G: ENFORCE capacity exhaustion -> fail-closed, ZERO loss (F1) ──
# Runs last (see ordering note above). Drives an UNCONVICTABLE transform (stream_transform oneshot:
# no resident sigma-state -> no key capture) so the ONLY thing that can refuse a write is capacity
# exhaustion -- isolating the F1 fail-closed path rather than a conviction (forward) block.
Write-Host "`nPhase G: ENFORCE capacity exhaustion -> fail-closed, ZERO loss" -ForegroundColor Yellow
$gdir="C:\sar\enforce_ovf"; $gN=20; $gLen=131072
MakeCorpus $gdir $gN $gLen
VM { & C:\sar\sarctl.exe mode enforce 2>&1 | Out-Null; & C:\sar\sarctl.exe budget 86400 1 2>&1 | Out-Null }
VMArgs { param($d,$n) Start-Process -FilePath C:\sar\stream_transform.exe -ArgumentList @("chacha","20","oneshot",$d,"$n","0") -NoNewWindow -Wait } @($gdir,$gN)
DrainBarrier | Out-Null
$gBcap = EventClassCount "block-capacity" 256
$g = ClassifyCorpus $gdir $gN $gLen -AllowRefused
Metric "ENFORCE refused (unmodified original)" "$($g.refused)/$gN"
Metric "ENFORCE recovered-by-preserve" "$($g.byPre)/$gN"
Metric "ENFORCE recovered-by-key" "$($g.byKey)/$gN"
Metric "ENFORCE lost" "$($g.lost)/$gN"
Metric "block-capacity events (Phase-G-attributable)" "$gBcap"
$classified = $g.refused + $g.byKey + $g.byPre + $g.lost
Assert "set closure: every target classified" ($classified -eq $gN) "classified=$classified/$gN"
Assert "capacity fail-closed engaged (block-capacity telemetry)" ($gBcap -gt 0) "block-capacity=$gBcap"
Assert "some destructive writes refused (fail-closed, not silent loss)" ($g.refused -gt 0) "refused=$($g.refused)"
Assert "ENFORCE capacity exhaustion loses ZERO files" ($g.lost -eq 0) "lost=$($g.lost) samples=$($g.samples -join ',')"

# ── Phase G-audit: AUDIT never blocks the same overflow (window slides) ──
Write-Host "`nPhase G-audit: AUDIT overflow never blocks" -ForegroundColor Yellow
$adir="C:\sar\audit_ovf"; $aN=20; $aLen=131072
MakeCorpus $adir $aN $aLen
VM { & C:\sar\sarctl.exe mode audit 2>&1 | Out-Null; & C:\sar\sarctl.exe budget 86400 1 2>&1 | Out-Null }
$aEvBefore = EventClassCount "block-capacity" 256
VMArgs { param($d,$n) Start-Process -FilePath C:\sar\stream_transform.exe -ArgumentList @("chacha","20","oneshot",$d,"$n","0") -NoNewWindow -Wait } @($adir,$aN)
DrainBarrier | Out-Null
$aEvAfter = EventClassCount "block-capacity" 256
Metric "AUDIT block-capacity events (delta)" "$($aEvAfter - $aEvBefore)"
Assert "AUDIT overflow never blocks (no new capacity block)" (($aEvAfter - $aEvBefore) -eq 0) "delta=$($aEvAfter - $aEvBefore)"

Write-Host "`n=== stream+fail-closed: $pass passed, $fail failed ===" -ForegroundColor Cyan
if ($fail -gt 0) { exit 1 }
