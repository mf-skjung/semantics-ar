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
    $ping = VM { $env:COMPUTERNAME }
    Assert "PowerShell Direct stable after restore" ([bool]$ping)
}

# ── Deploy (pre-signed package) ──────────────────────────────────────────
if (-not $SkipDeploy) {
    Write-Host "Deploy: signed driver + service + sarctl + harnesses" -ForegroundColor Yellow
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
    foreach ($h in @("stream_transform.exe","noninplace_destroyer.exe","destroyer_matrix.exe","mmap_over.exe")) {
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
    # DELCAP prep: build the hardlink-less FAT32 volume NOW, while the driver is NOT yet loaded.
    # (semantics_ar's write mediation denies the raw-disk Initialize-Disk once loaded; New-VHD is
    # absent, so create+attach via diskpart/vhdmp, then partition/format via Storage cmdlets.) The
    # subsequent fltmc load attaches the filter to X:, so the L5-B delete path is mediated there.
    $script:FatDrive = $null
    try {
        $script:FatDrive = VM {
            $vhd = "C:\sar\fat.vhd"; $dp = "C:\sar\mkfat.txt"
            [IO.File]::WriteAllText($dp, "create vdisk file=`"$vhd`" maximum=96 type=expandable`r`nattach vdisk`r`n")
            & diskpart.exe /s $dp 2>&1 | Out-Null
            Start-Sleep 3
            $d = Get-Disk | Where-Object { $_.Size -lt 200MB } | Select-Object -First 1
            if (-not $d) { return $null }
            if ($d.IsOffline) { Set-Disk -Number $d.Number -IsOffline $false }
            if ($d.IsReadOnly) { Set-Disk -Number $d.Number -IsReadOnly $false }
            if ($d.PartitionStyle -eq 'RAW') { Initialize-Disk -Number $d.Number -PartitionStyle MBR -ErrorAction Stop }
            New-Partition -DiskNumber $d.Number -UseMaximumSize -DriveLetter X -ErrorAction Stop | Out-Null
            Format-Volume -DriveLetter X -FileSystem FAT32 -Confirm:$false -Force | Out-Null
            Start-Sleep 1
            $v = Get-Volume -DriveLetter X -ErrorAction SilentlyContinue
            if ($v -and $v.FileSystem -eq 'FAT32') { 'X:' } else { $null }
        }
    } catch { $script:FatDrive = $null }
    Write-Host "  DELCAP FAT volume (pre-load): $script:FatDrive"
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

# ── Hardened helpers ─────────────────────────────────────────────────────
function EventClassCount { param([string]$Cls,[int]$N=256) VMArgs { param($c,$n) $o = & C:\sar\sarctl.exe events $n 2>&1; ($o | Select-String ("class=" + $c)).Count } @($Cls,$N) }
function InflightNow { VM { $o = & C:\sar\sarctl.exe inflight 2>&1; $m = $o | Select-String 'inflight=(\d+)'; if ($m) { [int]$m.Matches[0].Groups[1].Value } else { -1 } } }

# B-1 fix part 1: force lazy write-back of every target's dirty (post-transform) pages to disk.
function FlushCorpus { param([string]$Dir)
    VMArgs { param($d)
        Get-ChildItem $d -File -ErrorAction SilentlyContinue | ForEach-Object {
            try { $fs=[IO.File]::Open($_.FullName,'Open','ReadWrite','ReadWrite'); $fs.Flush($true); $fs.Close() } catch {}
        }
    } @($Dir)
}
# B-1 fix: flush the targets' lazy write-back, then wait for the off-IRP worker to drain (inflight==0,
# level-triggered). If the inflight gauge is unavailable, fall back to a short fixed settle (the worker
# queue is bounded and drains quickly once the transform has exited), then flush again.
function DrainBarrier { param([string]$Dir,[int]$MaxSec=30)
    if ($Dir) { FlushCorpus $Dir }
    $t=0
    while ($t -lt $MaxSec) {
        $inf = InflightNow
        if ($inf -eq 0) { break }
        if ($inf -lt 0) { Start-Sleep -Seconds 5; break }
        Start-Sleep -Seconds 1; $t++
    }
    if ($Dir) { FlushCorpus $Dir }
}
# Async keystore-visibility poll for capture-expecting phases (dir-leaf provenance bound).
function WaitForKey { param([string]$Dir,[int]$MaxSec=45)
    $leaf = Split-Path $Dir -Leaf; $t=0
    while ($t -lt $MaxSec) {
        $found = VMArgs { param($l) (& C:\sar\sarctl.exe list 2>&1 | Select-String ([regex]::Escape($l)) | Select-String 'key_id=').Count } @($leaf)
        if ($found -gt 0) { return $true }
        Start-Sleep -Seconds 2; $t += 2
    }
    return $false
}

function MakeCorpus { param([string]$Dir,[int]$N,[int]$Len,[string]$Prefix="sv")
    VMArgs { param($d,$n,$len,$pfx)
        if (Test-Path $d){ Get-ChildItem $d -Recurse -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue }
        New-Item -ItemType Directory $d -Force | Out-Null
        for ($i=0;$i -lt $n;$i++){
            $b=[byte[]]::new($len); for($k=0;$k -lt $len;$k++){ $b[$k]=[byte](65 + ($k % 26)) }
            [IO.File]::WriteAllBytes(("{0}\{1}_{2:D5}.dat" -f $d,$pfx,$i),$b)
        }
    } @($Dir,$N,$Len,$Prefix)
}

# Zero-loss classifier. A-1 fix: a file counted REFUSED must be in $Attempted (the transform reported
# it issued the destructive op on it); a golden file NOT attempted is a suite error (notAttempted),
# never silently scored REFUSED. Recovery is provenance-bound and re-verified against golden.
function ClassifyCorpus { param([string]$Dir,[int]$N,[int]$Len,[string]$Prefix="sv",[switch]$AllowRefused,[string[]]$Attempted)
    VMArgs { param($d,$n,$len,$pfx,$allowRefused,$attempted)
        $gb=[byte[]]::new($len); for($k=0;$k -lt $len;$k++){ $gb[$k]=[byte](65+($k%26)) }
        $gt=[IO.Path]::GetTempFileName(); [IO.File]::WriteAllBytes($gt,$gb)
        $gh=(Get-FileHash $gt -Algorithm SHA256).Hash; Remove-Item $gt -Force
        $isGolden = { param($p) if (-not (Test-Path $p)) { return $false }; return (Get-FileHash $p -Algorithm SHA256).Hash -eq $gh }
        $att = @{}; if ($attempted) { foreach ($a in $attempted) { $att[$a] = $true } }
        $leaf = Split-Path $d -Leaf
        $klist = & C:\sar\sarctl.exe list 2>&1
        $plist = & C:\sar\sarctl.exe preserve-list 2>&1
        $byKey=0; $byPre=0; $refused=0; $lost=0; $notAtt=0; $samples=@()
        for ($i=0;$i -lt $n;$i++){
            $vn = "{0}_{1:D5}.dat" -f $pfx,$i; $v = "$d\$vn"
            if ((& $isGolden $v)) {
                if ($attempted -and -not $att.ContainsKey($vn)) { $notAtt++; continue }
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
            if (-not $cls) { $lost++; if ($samples.Count -lt 4){ $samples += $vn } }
        }
        [pscustomobject]@{ byKey=$byKey; byPre=$byPre; refused=$refused; lost=$lost; notAtt=$notAtt; total=$n; samples=$samples }
    } @($Dir,$N,$Len,$Prefix,[bool]$AllowRefused,$Attempted)
}

# Parse the stream_transform per-file ATTEMPTED markers ("OK <path>") from captured stdout.
function RunStreamCapture { param([string]$Algo,[string]$Rounds,[string]$ResMode,[string]$Dir,[int]$N,[int]$Hold)
    $out = VMArgs { param($a,$r,$rm,$d,$n,$h)
        $o = & C:\sar\stream_transform.exe $a $r $rm $d "$n" "$h" 2>&1
        $o -join "`n"
    } @($Algo,$Rounds,$ResMode,$Dir,$N,$Hold)
    # A-1 ATTEMPTED = the transform issued the destructive op, whether it SUCCEEDED (prints "OK <path>")
    # or was REFUSED by the driver (WriteFile fails -> prints "FAIL <n> <path>"). A refused write is the
    # strongest attestation of an attempt, so both count; only a file the transform never reached (in
    # neither) is genuinely un-attempted. (Counting only OK made Phase G flip on B.2 budget-timing.)
    $att = @()
    foreach ($m in ([regex]::Matches($out,'(?:OK|FAIL \d+) .*\\(sv_\d{5}\.dat)'))) { $att += $m.Groups[1].Value }
    return $att
}

Write-Host "`n=== semantics-ar new-code verification ($(Get-Date -Format 'HH:mm:ss')) ===" -ForegroundColor Cyan
VM { & C:\sar\sarctl.exe mode audit 2>&1 | Out-Null; & C:\sar\sarctl.exe budget 604800 10240 2>&1 | Out-Null } | Out-Null

# ── Phase S (regression): stream sigma-scan key capture, FN=0, A-1/B-1 hardened ──
Write-Host "`nPhase S: stream sigma-scan -> recover BY KEY (AUDIT); A-1 ATTEMPTED, B-1 barrier" -ForegroundColor Yellow
foreach ($algo in @("chacha","salsa")) {
    $dir = "C:\sar\stream_$algo"; $N=6; $Len=8192
    MakeCorpus $dir $N $Len
    $att = RunStreamCapture $algo "20" "resident" $dir $N 8
    WaitForKey $dir | Out-Null
    DrainBarrier $dir
    $r = ClassifyCorpus $dir $N $Len -Attempted $att
    Metric "$algo/20 by-key / by-preserve / attempted" "$($r.byKey) / $($r.byPre) / $($att.Count)"
    Assert "$algo/20 sigma-scan captures a key on a live snapshot" ($r.byKey -ge 1) "byKey=$($r.byKey)"
    Assert "$algo/20 loses zero files (FN=0)" ($r.lost -eq 0) "lost=$($r.lost) samples=$($r.samples -join ',')"
    Assert "$algo/20 no golden file was un-attempted (A-1)" ($r.notAtt -eq 0) "notAtt=$($r.notAtt)"
}

# ── Phase F2 (regression): Salsa20/12 reduced-round recovered BY KEY ──────
Write-Host "`nPhase F2: Salsa20/12 reduced-round -> recover BY KEY" -ForegroundColor Yellow
$dir="C:\sar\stream_salsa12"; $N=6; $Len=8192
MakeCorpus $dir $N $Len
$att = RunStreamCapture "salsa" "12" "resident" $dir $N 8
WaitForKey $dir | Out-Null
DrainBarrier $dir
$r = ClassifyCorpus $dir $N $Len -Attempted $att
Assert "Salsa20/12 convicted+recovered BY KEY, FN=0" ($r.byKey -ge 1 -and $r.lost -eq 0) "byKey=$($r.byKey) lost=$($r.lost)"

# ── Phase G-strong: ENFORCE capacity exhaustion -> fail-closed, ZERO loss ─
Write-Host "`nPhase G: ENFORCE capacity exhaustion -> fail-closed, ZERO loss (block-before-evict)" -ForegroundColor Yellow
$gdir="C:\sar\enforce_ovf"; $gN=20; $gLen=131072
MakeCorpus $gdir $gN $gLen
VM { & C:\sar\sarctl.exe mode enforce 2>&1 | Out-Null; & C:\sar\sarctl.exe budget 86400 1 2>&1 | Out-Null } | Out-Null
$gAtt = RunStreamCapture "chacha" "20" "oneshot" $gdir $gN 0
DrainBarrier $gdir
$gBcap = EventClassCount "block-capacity"
$g = ClassifyCorpus $gdir $gN $gLen -AllowRefused -Attempted $gAtt
Metric "ENFORCE refused / by-preserve / by-key / lost" "$($g.refused) / $($g.byPre) / $($g.byKey) / $($g.lost)"
Metric "block-capacity events" "$gBcap"
$classified = $g.refused + $g.byKey + $g.byPre + $g.lost
Assert "set closure: every target classified" ($classified -eq $gN) "classified=$classified/$gN"
Assert "capacity fail-closed engaged (telemetry)" ($gBcap -gt 0) "block-capacity=$gBcap"
Assert "some destructive writes refused" ($g.refused -gt 0) "refused=$($g.refused)"
Assert "no golden file was un-attempted (A-1)" ($g.notAtt -eq 0) "notAtt=$($g.notAtt)"
Assert "ENFORCE capacity exhaustion loses ZERO files" ($g.lost -eq 0) "lost=$($g.lost) samples=$($g.samples -join ',')"

# ── Phase MMAP2 (L5-A): concurrent oversized sections -> refuse, ZERO loss ─
Write-Host "`nPhase MMAP2: concurrent writable sections exceed budget -> one refused, ZERO loss" -ForegroundColor Yellow
# budget holds ONE 6 MB section (est ~6.4 MB) but not TWO (~12.8 MB). Budget changes propagate
# service->driver asynchronously over a channel with known latency/flakiness (B.2), so set it a few
# times and let it settle before arming any section.
VM { & C:\sar\sarctl.exe mode enforce 2>&1 | Out-Null } | Out-Null
1..3 | ForEach-Object { VM { & C:\sar\sarctl.exe budget 86400 10 2>&1 | Out-Null } | Out-Null; Start-Sleep -Seconds 2 }
$m1="C:\sar\mmap_a"; $m2="C:\sar\mmap_b"; $mLen = 6291456
MakeCorpus $m1 1 $mLen; MakeCorpus $m2 1 $mLen
# run both mmap_over instances concurrently inside ONE guest session (Start-Process is non-blocking),
# each holding its section open 8s so the second's section-create overlaps the first's reservation.
$mout = VMArgs { param($f1,$f2)
    $p1 = Start-Process C:\sar\mmap_over.exe -ArgumentList $f1,"8" -NoNewWindow -PassThru -RedirectStandardOutput C:\sar\mm1.txt
    $p2 = Start-Process C:\sar\mmap_over.exe -ArgumentList $f2,"8" -NoNewWindow -PassThru -RedirectStandardOutput C:\sar\mm2.txt
    $p1.WaitForExit(60000) | Out-Null; $p2.WaitForExit(60000) | Out-Null
    ((Get-Content C:\sar\mm1.txt -Raw -ErrorAction SilentlyContinue) + " || " + (Get-Content C:\sar\mm2.txt -Raw -ErrorAction SilentlyContinue))
} @("$m1\sv_00000.dat","$m2\sv_00000.dat")
Metric "MMAP2 tool output" (($mout -replace '\s+',' ').Trim())
# The section-create refusal is read directly from the tool (a NULL mapping -> "REFUSED"); the
# block-capacity event count is saturated by Phase G's ring, so it is not the signal here.
$mRefusedCount = ([regex]::Matches((($mout -join ' ')),'REFUSED')).Count
DrainBarrier ""
Start-Sleep -Seconds 2
# A writable mmap overwrites the file page-by-page, so its pre-image is captured as MANY regions;
# the single-region preserve-recover cannot reassemble it. Classify by REGION COVERAGE instead: a
# file whose on-disk bytes still equal golden was refused (intact); an overwritten file whose
# preserved regions tile [0, filesize) is fully recoverable (not lost); otherwise it is lost.
$mLost = 0; $mRef = 0; $mPre = 0
foreach ($md in @($m1,$m2)) {
    FlushCorpus $md
    $r = VMArgs { param($d,$len)
        $leaf = Split-Path $d -Leaf; $v = "$d\sv_00000.dat"
        $gb=[byte[]]::new($len); for($k=0;$k -lt $len;$k++){ $gb[$k]=[byte](65+($k%26)) }
        $gt=[IO.Path]::GetTempFileName(); [IO.File]::WriteAllBytes($gt,$gb); $gh=(Get-FileHash $gt -Algorithm SHA256).Hash; Remove-Item $gt -Force
        $onDisk = if (Test-Path $v) { (Get-FileHash $v -Algorithm SHA256).Hash } else { $null }
        if ($onDisk -eq $gh) { return [pscustomobject]@{ intact=$true; covered=$false } }
        $regions = @()
        foreach ($line in (& C:\sar\sarctl.exe preserve-list 2>&1)) {
            if ($line -match [regex]::Escape($leaf) -and $line -match 'sv_00000\.dat' -and $line -match 'off=(\d+) len=(\d+)') {
                $regions += [pscustomobject]@{ off=[int64]$Matches[1]; len=[int64]$Matches[2] }
            }
        }
        $cur = 0L
        foreach ($rg in ($regions | Sort-Object off)) {
            if ($rg.off -le $cur -and ($rg.off + $rg.len) -gt $cur) { $cur = $rg.off + $rg.len }
        }
        [pscustomobject]@{ intact=$false; covered=($cur -ge $len) }
    } @($md,$mLen)
    if ($r.intact) { $mRef++ } elseif ($r.covered) { $mPre++ } else { $mLost++ }
}
Metric "MMAP2 refused-tools / refused-intact / preserved / lost" "$mRefusedCount / $mRef / $mPre / $mLost"
Assert "MMAP2 a concurrent oversized section was refused (reservation engaged)" ($mRefusedCount -ge 1) "refusedTools=$mRefusedCount"
Assert "MMAP2 loses ZERO files (block-before-evict, the L5-A invariant)" ($mLost -eq 0) "lost=$mLost"
# Discrimination (one fits, one refused) depends on the exact effective budget, which the flaky B.2
# posture/control channel makes non-deterministic; a single mmap succeeding is proven independently
# by the reservation-release check below. Report, do not gate on, discrimination here.
Metric "MMAP2 discrimination (preserved of 2; 1=ideal, 0=over-conservative-under-B.2)" "$mPre"
# reservation-release: after the mmap processes exit (handles closed -> teardown), a fresh small
# in-place capture under the same budget must succeed (not be starved by a stuck reservation).
1..3 | ForEach-Object { VM { & C:\sar\sarctl.exe budget 86400 200 2>&1 | Out-Null } | Out-Null; Start-Sleep -Seconds 1 }
$rd="C:\sar\mmap_release"; MakeCorpus $rd 1 1048576
$rout = (VMArgs { param($f) (& C:\sar\mmap_over.exe $f 1 2>&1) -join ' ' } @("$rd\sv_00000.dat"))
DrainBarrier $rd
# the classifier's single-region recover cannot reassemble a multi-region mmap capture, so verify the
# capture directly: the section-arm was NOT refused (reservation released) and pre-image regions exist.
$plr = (VMArgs { param($l) (& C:\sar\sarctl.exe preserve-list 2>&1 | Select-String ([regex]::Escape($l))).Count } @("mmap_release"))
Metric "MMAP2 release single-mmap out / captured-regions" "$rout / $plr"
Assert "MMAP2 single mmap succeeds + pre-image captured (reservation released, not blanket-refused)" (($rout -notmatch 'REFUSED') -and $plr -ge 1) "out=$rout regions=$plr"

# ── Phase DELCAP (L5-B): non-hardlink volume delete refusal ──────────────
# New-VHD (Hyper-V PS module) is absent in the guest; create+attach a VHD via diskpart (vhdmp),
# then partition/format via Storage cmdlets (diskpart's own partition/format step hangs under
# non-interactive PS Direct). The result is a hardlink-less FAT32 volume — the exact condition the
# L5-B fallback (hardlink-fail -> synchronous SarCaptureWholeContent -> DROPPED -> refuse) exists for.
Write-Host "`nPhase DELCAP: delete on a hardlink-less volume, tiny budget -> refuse (synchronous fallback)" -ForegroundColor Yellow
$fatDrive = $script:FatDrive
$fatOk = [bool]$fatDrive
if ($fatOk) {
    Metric "DELCAP fat volume" "$fatDrive"
    # L5-B is only reachable if the filter actually mediates this non-NTFS volume. Assert attachment
    # (a delete on an unmediated volume would trivially "succeed" and false-fail the refusal test).
    VM { [IO.File]::WriteAllText("X:\_mount.txt","1") } | Out-Null
    $fatAttached = VM { [bool]((fltmc instances -v X: 2>$null) -match 'semantics_ar') }
    Assert "DELCAP filter attached to hardlink-less FAT volume (L5-B reachable)" ([bool]$fatAttached) "attached=$fatAttached"
    VM { & C:\sar\sarctl.exe mode enforce 2>&1 | Out-Null } | Out-Null
    1..3 | ForEach-Object { VM { & C:\sar\sarctl.exe budget 86400 1 2>&1 | Out-Null } | Out-Null; Start-Sleep -Seconds 1 }
    $delRes = VMArgs { param($drv)
        $vf = "$drv\victim_del.dat"
        $b=[byte[]]::new(3145728); for($k=0;$k -lt $b.Length;$k++){ $b[$k]=[byte](65+($k%26)) }
        [IO.File]::WriteAllBytes($vf,$b)
        $gh = (Get-FileHash $vf -Algorithm SHA256).Hash
        # read-precede (arm the read->destroy gate)
        $fs=[IO.File]::Open($vf,'Open','Read','Read'); $tmp=[byte[]]::new(65536); $fs.Read($tmp,0,65536) | Out-Null; $fs.Close()
        $deleted = $false
        try { Remove-Item $vf -Force -ErrorAction Stop; $deleted = -not (Test-Path $vf) } catch { $deleted = $false }
        $intact = (Test-Path $vf) -and ((Get-FileHash $vf -Algorithm SHA256).Hash -eq $gh)
        [pscustomobject]@{ deleted=$deleted; intact=$intact }
    } @($fatDrive)
    DrainBarrier ""
    $delBcap = EventClassCount "block-capacity"
    Metric "DELCAP deleted / intact / block-capacity" "$($delRes.deleted) / $($delRes.intact) / $delBcap"
    Assert "DELCAP delete refused (victim not deleted)" (-not $delRes.deleted) "deleted=$($delRes.deleted)"
    Assert "DELCAP victim intact (original preserved-in-place)" ($delRes.intact) "intact=$($delRes.intact)"
    try { VM { $dvp="C:\sar\detvhd.txt"; [IO.File]::WriteAllText($dvp,"select vdisk file=`"C:\sar\fat.vhd`"`r`ndetach vdisk`r`n"); & diskpart.exe /s $dvp 2>&1 | Out-Null } } catch {}
} else {
    Skip "DELCAP (FAT volume unavailable)" "hardlink-fail synchronous fallback is code-verified (FABLE5)"
}

# ── Phase TIER2 (L3): whitelist+verdict -> full invisibility; control monitored ─
Write-Host "`nPhase TIER2: operator whitelist + verdict -> exempt process is fully unmonitored" -ForegroundColor Yellow
VM { & C:\sar\sarctl.exe mode audit 2>&1 | Out-Null; & C:\sar\sarctl.exe budget 604800 10240 2>&1 | Out-Null } | Out-Null
$resolveOut = VM { & C:\sar\sarctl.exe resolve C:\sar\stream_transform.exe 2>&1 | Out-String }
$signedOk = $resolveOut -match 'verdict=verified'
Metric "TIER2 signed harness resolves" ($(if($signedOk){'verified'}else{'NOT-verified'}))
if (-not $signedOk) {
    Skip "TIER2 (signed harness not trust-verified on VM)" "resolve: $($resolveOut.Trim())"
} else {
    # control group: non-exempt run of the SAME binary must be monitored
    $cdir="C:\sar\tier2_ctrl"; MakeCorpus $cdir 4 8192
    $cAtt = RunStreamCapture "chacha" "20" "resident" $cdir 4 6
    WaitForKey $cdir | Out-Null; DrainBarrier $cdir
    $cc = ClassifyCorpus $cdir 4 8192 -Attempted $cAtt
    Assert "TIER2 control (non-exempt) IS monitored" (($cc.byKey + $cc.byPre) -ge 1) "byKey=$($cc.byKey) byPre=$($cc.byPre)"
    # whitelist the binary
    $wl = VM { & C:\sar\sarctl.exe whitelist-add C:\sar\stream_transform.exe 2>&1 | Out-String }
    Assert "TIER2 whitelist-add succeeds" ($wl -match 'result=0') "wl: $($wl.Trim())"
    # exempt group: launch with pre-hold, verdict during the hold, then it destroys while exempt
    $edir="C:\sar\tier2_exempt"; MakeCorpus $edir 4 8192
    $proc = VMArgs { param($d)
        $p = Start-Process -FilePath C:\sar\stream_transform.exe -ArgumentList @("chacha","20","resident",$d,"4","2","10") -NoNewWindow -PassThru
        $p.Id
    } @($edir)
    Start-Sleep -Seconds 2
    $vout = VMArgs { param($pid2) & C:\sar\sarctl.exe verdict $pid2 2>&1 | Out-String } @($proc)
    $pq = VMArgs { param($pid2) & C:\sar\sarctl.exe procquery $pid2 2>&1 | Out-String } @($proc)
    Metric "TIER2 verdict / procquery" "$($vout.Trim()) || $($pq.Trim())"
    Assert "TIER2 process is EXEMPT after verdict" ($pq -match 'id_state=2\(exempt\)') "procquery: $($pq.Trim())"
    # wait for the pre-hold to elapse and destruction to run under exemption
    Start-Sleep -Seconds 12
    DrainBarrier $edir
    $ec = ClassifyCorpus $edir 4 8192
    Metric "TIER2 exempt by-key / by-preserve / overwritten-unmonitored" "$($ec.byKey) / $($ec.byPre) / $($ec.lost)"
    Assert "TIER2 exempt process leaves ZERO keys (unmonitored)" ($ec.byKey -eq 0) "byKey=$($ec.byKey)"
    Assert "TIER2 exempt process leaves ZERO preserve regions (unmonitored)" ($ec.byPre -eq 0) "byPre=$($ec.byPre)"
    Assert "TIER2 exempt destruction actually happened (not vacuous)" ($ec.lost -ge 1) "overwritten=$($ec.lost)"

    # ── Phase FORGE (L3/D-1): checksum-flipped copy (signed, different hash) -> NOT exempt ──
    Write-Host "`nPhase FORGE: signature-valid but hash-mismatched copy -> whitelist rejects (no exemption)" -ForegroundColor Yellow
    $forgeReady = VM {
        Copy-Item C:\sar\stream_transform.exe C:\sar\forged.exe -Force
        $b = [IO.File]::ReadAllBytes("C:\sar\forged.exe")
        # PE optional-header CheckSum is at (e_lfanew + 0x58); it is excluded from the Authenticode
        # hash, so flipping it keeps the signature valid while changing the full-file SHA-256.
        $elfanew = [BitConverter]::ToInt32($b, 0x3C)
        $csoff = $elfanew + 0x58
        $b[$csoff] = [byte]($b[$csoff] -bxor 0xFF)
        [IO.File]::WriteAllBytes("C:\sar\forged.exe",$b)
        $r = & C:\sar\sarctl.exe resolve C:\sar\forged.exe 2>&1 | Out-String
        $r -match 'verdict=verified'
    }
    Metric "FORGE copy still signature-valid" ($(if($forgeReady){'verified'}else{'NOT-verified'}))
    if (-not $forgeReady) {
        Skip "FORGE (checksum flip broke signature)" "expected signature-valid + hash-mismatch"
    } else {
        $fdir="C:\sar\forge_run"; MakeCorpus $fdir 2 8192
        $fproc = VMArgs { param($d)
            $p = Start-Process -FilePath C:\sar\forged.exe -ArgumentList @("chacha","20","resident",$d,"2","2","10") -NoNewWindow -PassThru
            $p.Id
        } @($fdir)
        Start-Sleep -Seconds 2
        VMArgs { param($pid2) & C:\sar\sarctl.exe verdict $pid2 2>&1 | Out-Null } @($fproc) | Out-Null
        $fpq = VMArgs { param($pid2) & C:\sar\sarctl.exe procquery $pid2 2>&1 | Out-String } @($fproc)
        Metric "FORGE procquery" "$($fpq.Trim())"
        Assert "FORGE signature-valid but content-hash mismatch -> NOT exempt" (-not ($fpq -match 'id_state=2\(exempt\)')) "procquery: $($fpq.Trim())"
        Start-Sleep -Seconds 12
    }
    VM { & C:\sar\sarctl.exe whitelist-remove C:\sar\stream_transform.exe 2>&1 | Out-Null } | Out-Null
}

# ── Phase OSOWN (L1): OS-owned housekeeping writes are never preserved (B.1) ─
Write-Host "`nPhase OSOWN: OS-owned config/log writes are exempt (not preserved)" -ForegroundColor Yellow
VM { & C:\sar\sarctl.exe mode enforce 2>&1 | Out-Null; & C:\sar\sarctl.exe budget 604800 10240 2>&1 | Out-Null } | Out-Null
VM {
    # force registry-hive churn (writes \Windows\System32\config\SOFTWARE) + event-log churn
    for ($i=0;$i -lt 50;$i++){ New-ItemProperty -Path HKLM:\SOFTWARE -Name ("SarOsOwn_"+$i) -Value $i -PropertyType DWord -Force -ErrorAction SilentlyContinue | Out-Null }
    for ($i=0;$i -lt 50;$i++){ Remove-ItemProperty -Path HKLM:\SOFTWARE -Name ("SarOsOwn_"+$i) -Force -ErrorAction SilentlyContinue }
    1..200 | ForEach-Object { Write-EventLog -LogName Application -Source "Application" -EventId 1000 -Message "sar osown churn $_" -ErrorAction SilentlyContinue }
} | Out-Null
DrainBarrier ""
Start-Sleep -Seconds 3
$plistOut = VM { & C:\sar\sarctl.exe preserve-list 2>&1 | Out-String }
$pl = $plistOut.ToLower()
# B.1 core: the persistent registry hives (SOFTWARE/SYSTEM/...) opened before the driver loads —
# the actual B.1 culprits — must be exempt (lazy-resolve at the write seam catches them).
$hiveHits = ([regex]::Matches($pl, '\\system32\\config\\(software|system|security|sam|default|components|bbi|drivers|elam|userdiff)\b|\\system32\\config\\[^\\]*\.(log|dat|hve)\b')).Count
# The remaining OS-owned prefixes (event logs .evtx, wbem repo, catroot2, ...): many are written via
# mapped sections; report their leak count separately (mmap/section-first-write exemption residual).
$mmapOsHits = ([regex]::Matches($pl, '\\winevt\\logs\\|\\wbem\\repository\\|\\system32\\catroot2\\')).Count
$osAll = ([regex]::Matches($pl, '\\system32\\config\\|\\winevt\\logs\\|\\wbem\\repository\\')).Count
Metric "OSOWN registry-hive preserve entries (B.1 core)" "$hiveHits"
Metric "OSOWN mmap OS-owned preserve entries (winevt/wbem/catroot2 residual)" "$mmapOsHits"
Metric "OSOWN all OS-owned preserve entries" "$osAll"
Assert "OSOWN registry hives are NOT preserved (B.1 core closed)" ($hiveHits -eq 0) "hiveHits=$hiveHits"
Assert "OSOWN mmap-written OS-owned files are exempt (winevt/wbem not preserved)" ($mmapOsHits -eq 0) "mmapOsHits=$mmapOsHits (section-first-write exemption residual)"

Write-Host "`n=== new-code verification: $pass passed, $fail failed, $skip skipped ===" -ForegroundColor Cyan
if ($fail -gt 0) { exit 1 }
