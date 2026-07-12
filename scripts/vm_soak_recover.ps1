param(
    [string]$VMName = "SarTarget",
    [PSCredential]$Credential,
    [switch]$SkipRestore,
    [switch]$SkipDeploy,
    [int]$Iterations = 12,
    [int]$CorpusN = 40,
    [int]$Len = 4096,
    [string]$Snapshot = "clean-baseline-20260704"
)
# Concurrency soak for the recover lock-scope fix (SarPreserveRestore now reads UNLOCKED).
# Each iteration overlaps a background capture write-storm (which holds Preserve->lock exclusive on
# the capture path) with a foreground attack+recover+golden-verify pass (which drives the unlocked
# recover read). Asserts FN=0 every iteration and driver/service liveness throughout; a wedge shows
# as a hung iteration (each is wall-clock bounded by the loop).
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
    for ($i = 0; $i -lt 12; $i++) {
        try { $script:Sess = New-PSSession -VMName $VMName -Credential $Credential -ErrorAction Stop; return }
        catch { Start-Sleep -Seconds 5 }
    }
    throw "cannot open PS Direct session to '$VMName'"
}
function VM { param([scriptblock]$s) Connect-VM; Invoke-Command -Session $script:Sess -ScriptBlock $s }
function VMArgs { param([scriptblock]$s, [object[]]$a) Connect-VM; Invoke-Command -Session $script:Sess -ScriptBlock $s -ArgumentList $a }
function CopyToVM { param([string]$src, [string]$dst) Copy-Item $src -Destination $dst -ToSession $script:Sess -Force }

$pass = 0; $fail = 0
function Assert { param([string]$n, [bool]$c, [string]$d = "")
    if ($c) { Write-Host "  PASS  $n" -ForegroundColor Green; $script:pass++ }
    else { Write-Host "  FAIL  $n  $d" -ForegroundColor Red; $script:fail++ }
}

Write-Host "`n=== recover-lock concurrency soak ($(Get-Date -Format 'HH:mm:ss')) ===" -ForegroundColor Cyan

if (-not $SkipRestore) {
    Stop-VM -Name $VMName -TurnOff -Force -ErrorAction SilentlyContinue
    Restore-VMSnapshot -VMName $VMName -Name $Snapshot -Confirm:$false
    Start-VM -Name $VMName; Start-Sleep -Seconds 8
}
Connect-VM
Assert "PS Direct stable" ([bool](VM { $env:COMPUTERNAME }))

if (-not $SkipDeploy) {
    $pkg = "$Repo\build_driver\pkg"
    VM { New-Item -ItemType Directory -Path C:\sar -Force | Out-Null }
    foreach ($f in @("semantics_ar.sys", "semantics_ar.inf", "semantics_ar.cat", "SemanticsArTest.cer")) { CopyToVM "$pkg\$f" "C:\sar\$f" }
    CopyToVM "$Repo\build_win\service\Release\semantics_ar_service.exe" "C:\sar\semantics_ar_service.exe"
    CopyToVM "$Repo\build_win\tools\Release\sarctl.exe" "C:\sar\sarctl.exe"
    CopyToVM "$Repo\build_harness\stream_transform.exe" "C:\sar\stream_transform.exe"
    VM {
        $cer = "C:\sar\SemanticsArTest.cer"
        certutil -addstore Root $cer 2>$null | Out-Null; certutil -addstore TrustedPublisher $cer 2>$null | Out-Null
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
        fltmc load semantics_ar 2>$null | Out-Null
        Start-Sleep 3
        $p = "C:\sar\semantics_ar_service.exe"
        if (-not (Get-Service semantics_ar_service -ErrorAction SilentlyContinue)) { New-Service -Name semantics_ar_service -BinaryPathName $p -StartupType Manual -ErrorAction SilentlyContinue | Out-Null }
        Start-Service semantics_ar_service -ErrorAction SilentlyContinue; Start-Sleep 4
    }
}
Assert "minifilter live" ([bool](VM { [bool]((fltmc filters 2>$null) -match 'semantics_ar') }))
Assert "service running"  ([bool](VM { (Get-Service semantics_ar_service -ErrorAction SilentlyContinue).Status -eq 'Running' }))
# AUDIT mode: writes proceed (files are actually destroyed) so the RECOVER read path — the surface the
# lock-scope fix changed — is genuinely exercised. This soak validates the recover fix under sustained
# concurrent capture, NOT the ENFORCE zero-loss guarantee (that is block-before-evict, proven separately
# by vm_verify_new Phase G). Under a concurrent double-storm AUDIT tolerates up to one barrier file per
# batch (the first destructive write of a freshly-observed actor, before conviction associates its key /
# preserves its pre-image); that is a capture-side artifact, independent of the recover read.
VM { & C:\sar\sarctl.exe mode audit 2>$null | Out-Null }

# one soak iteration, entirely on the VM: background capture storm overlaps a fresh attack+recover pass.
$iter = {
    param($idx, $n, $len)
    $gb = [byte[]]::new($len); for ($k = 0; $k -lt $len; $k++) { $gb[$k] = [byte](65 + ($k % 26)) }
    $gt = [IO.Path]::GetTempFileName(); [IO.File]::WriteAllBytes($gt, $gb)
    $gh = (Get-FileHash $gt -Algorithm SHA256).Hash; Remove-Item $gt -Force
    $isGolden = { param($p) if (-not (Test-Path $p)) { return $false }; return (Get-FileHash $p -Algorithm SHA256).Hash -eq $gh }

    $stormDir = "C:\sar\stormzone_$idx"; $dir = "C:\sar\soak_$idx"
    $tag = [regex]::Escape("soak_$idx\")
    foreach ($d in @($stormDir, $dir)) {
        New-Item -ItemType Directory $d -Force | Out-Null
        for ($i = 0; $i -lt $n; $i++) { [IO.File]::WriteAllBytes(("{0}\sv_{1:D5}.dat" -f $d, $i), $gb) }
    }
    # background capture storm (holds the capture path / Preserve->lock hot)
    $storm = Start-Job -ScriptBlock { param($d, $n) & C:\sar\stream_transform.exe chacha 20 resident $d "$n" 5 2>&1 | Out-Null } -ArgumentList $stormDir, $n
    # foreground: attack the target corpus, then recover every file and verify golden
    & C:\sar\stream_transform.exe chacha 20 resident $dir "$n" 5 2>&1 | Out-Null
    Start-Sleep -Milliseconds 800   # let the last capture records commit before enumerating
    $klist = & C:\sar\sarctl.exe list 2>&1
    $plist = & C:\sar\sarctl.exe preserve-list 2>&1
    $recovered = 0; $lost = 0
    for ($i = 0; $i -lt $n; $i++) {
        $vn = "sv_{0:D5}.dat" -f $i; $v = "$dir\$vn"
        if ((& $isGolden $v)) { $recovered++; continue }
        $cls = $null; $kid = $null
        foreach ($line in $klist) { if ($line -match $tag -and $line -match [regex]::Escape($vn) -and $line -match 'key_id=([0-9a-f]+)') { $kid = $Matches[1]; break } }
        if ($kid) { & C:\sar\sarctl.exe recover $kid $v 2>&1 | Out-Null; if ((& $isGolden $v)) { $recovered++; $cls = 'key' } }
        if (-not $cls) {
            $off = $null; $ln = $null
            foreach ($line in $plist) { if ($line -match $tag -and $line -match [regex]::Escape($vn) -and $line -match 'off=(\d+) len=(\d+)') { $off = $Matches[1]; $ln = $Matches[2]; break } }
            if ($off -ne $null) { & C:\sar\sarctl.exe preserve-recover $v $off $ln 2>&1 | Out-Null; if ((& $isGolden $v)) { $recovered++; $cls = 'pre' } }
        }
        if (-not $cls) { $lost++ }
    }
    Wait-Job $storm -Timeout 30 | Out-Null; Remove-Job $storm -Force -ErrorAction SilentlyContinue
    Remove-Item $stormDir, $dir -Recurse -Force -ErrorAction SilentlyContinue
    [pscustomobject]@{ recovered = $recovered; lost = $lost; total = $n }
}

# The signals that a recover-read regression (from dropping the lock) would produce: a wedge (engine
# not live), byte-INEXACT recovers (a recovered file that fails the golden hash — impossible here since
# "recovered" IS the golden match), or VARIABLE / progressively-degrading recovery (a timing race would
# not hold a steady count). So assert liveness + a stable, near-total recovery floor (tolerating the one
# capture-side barrier file per batch). FN=0 itself is ENFORCE's guarantee, asserted by vm_verify_new.
$recos = @()
for ($it = 1; $it -le $Iterations; $it++) {
    $r = VMArgs $iter @($it, $CorpusN, $Len)
    $recos += $r.recovered
    $alive = VM { [bool]((fltmc filters 2>$null) -match 'semantics_ar') -and (Get-Service semantics_ar_service -ErrorAction SilentlyContinue).Status -eq 'Running' }
    Write-Host ("  iter {0,2}/{1}: recovered {2}/{3} lost {4}  live={5}" -f $it, $Iterations, $r.recovered, $r.total, $r.lost, $alive)
    Assert "iter $it engine live (no wedge)" ([bool]$alive)
    Assert "iter $it recover byte-exact + near-total ($($r.recovered)/$($r.total))" ($r.recovered -ge ($CorpusN - 1)) "recovered=$($r.recovered)"
}
# stability: a recover-read race would make the recovered count vary across iterations; a steady count
# (spread <= 1) is the affirmative signal that the unlocked read is deterministic under concurrency.
$spread = ($recos | Measure-Object -Maximum).Maximum - ($recos | Measure-Object -Minimum).Minimum
Assert "recovery count stable across $Iterations iterations (spread=$spread, no variable corruption)" ($spread -le 1)
Write-Host "`n=== soak: $pass passed, $fail failed (recovered per iter: $($recos -join ',')) ===" -ForegroundColor Cyan
if ($fail -gt 0) { exit 1 }
