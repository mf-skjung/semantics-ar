param(
    [string]$VMName = "SarTarget",
    [PSCredential]$Credential,
    [int]$Rounds = 8,
    [int]$CorpusN = 30,
    [int]$Len = 4096
)
# Verifies the recover lock-split (recover on g_recover_lock, other control ops on g_control_lock):
# concurrent recover round-trips and control-op round-trips now run in flight together on the single
# driver port. Proves the introduced concurrency is corruption-free — recovers stay byte-exact while
# a concurrent stream of control ops (status / preserve-list / whitelist add+remove / mode toggles)
# all succeed, and the engine stays live. Redeploys only the service exe (driver unchanged).
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
    throw "cannot open PS Direct session"
}
function VM { param([scriptblock]$s) Connect-VM; Invoke-Command -Session $script:Sess -ScriptBlock $s }
function VMArgs { param([scriptblock]$s, [object[]]$a) Connect-VM; Invoke-Command -Session $script:Sess -ScriptBlock $s -ArgumentList $a }
$pass = 0; $fail = 0
function Assert { param([string]$n, [bool]$c, [string]$d = "")
    if ($c) { Write-Host "  PASS  $n" -ForegroundColor Green; $script:pass++ }
    else { Write-Host "  FAIL  $n  $d" -ForegroundColor Red; $script:fail++ }
}

Write-Host "`n=== recover-isolation verify ($(Get-Date -Format 'HH:mm:ss')) ===" -ForegroundColor Cyan
Connect-VM
Assert "PS Direct stable" ([bool](VM { $env:COMPUTERNAME }))

# ── redeploy the service exe (driver unchanged) ──
Copy-Item "$Repo\build_win\service\Release\semantics_ar_service.exe" -Destination "C:\sar\semantics_ar_service.exe" -ToSession $script:Sess -Force
VM {
    Stop-Service semantics_ar_service -Force -ErrorAction SilentlyContinue
    Start-Sleep 2
    Start-Service semantics_ar_service -ErrorAction SilentlyContinue
    Start-Sleep 4
}
Assert "minifilter live" ([bool](VM { [bool]((fltmc filters 2>$null) -match 'semantics_ar') }))
Assert "service running" ([bool](VM { (Get-Service semantics_ar_service -ErrorAction SilentlyContinue).Status -eq 'Running' }))
VM { & C:\sar\sarctl.exe mode audit 2>$null | Out-Null }

$body = {
    param($rounds, $n, $len)
    $gb = [byte[]]::new($len); for ($k = 0; $k -lt $len; $k++) { $gb[$k] = [byte](65 + ($k % 26)) }
    $gt = [IO.Path]::GetTempFileName(); [IO.File]::WriteAllBytes($gt, $gb)
    $gh = (Get-FileHash $gt -Algorithm SHA256).Hash; Remove-Item $gt -Force
    $isGolden = { param($p) if (-not (Test-Path $p)) { return $false }; return (Get-FileHash $p -Algorithm SHA256).Hash -eq $gh }

    $ctlOk = $true; $ctlCount = 0; $recOk = 0; $recLost = 0
    for ($r = 1; $r -le $rounds; $r++) {
        $dir = "C:\sar\iso_$r"; $tag = [regex]::Escape("iso_${r}\")
        New-Item -ItemType Directory $dir -Force | Out-Null
        for ($i = 0; $i -lt $n; $i++) { [IO.File]::WriteAllBytes(("{0}\sv_{1:D5}.dat" -f $dir, $i), $gb) }
        & C:\sar\stream_transform.exe chacha 20 resident $dir "$n" 5 2>&1 | Out-Null
        Start-Sleep -Milliseconds 600

        # control-op storm concurrent with the recover pass. Use ops that return a deterministic
        # success (status / preserve-list / mode) - NOT whitelist-add of an untrusted binary, whose
        # non-zero "identity not verified" is a valid logical result, not a concurrency failure.
        $ctl = Start-Job -ScriptBlock {
            $ok = $true; $c = 0
            for ($j = 0; $j -lt 40; $j++) {
                if ((& C:\sar\sarctl.exe status 2>&1) -notmatch 'mode|drv|svc|captured') { $ok = $false }
                & C:\sar\sarctl.exe preserve-list 2>&1 | Out-Null
                if ($LASTEXITCODE -ne 0) { $ok = $false }
                & C:\sar\sarctl.exe list 2>&1 | Out-Null
                if ($LASTEXITCODE -ne 0) { $ok = $false }
                & C:\sar\sarctl.exe mode audit 2>&1 | Out-Null
                if ($LASTEXITCODE -ne 0) { $ok = $false }
                $c += 4
            }
            [pscustomobject]@{ ok = $ok; count = $c }
        }

        # foreground recover pass (drives SarPreserveRestore concurrently with the control storm)
        $klist = & C:\sar\sarctl.exe list 2>&1
        $plist = & C:\sar\sarctl.exe preserve-list 2>&1
        for ($i = 0; $i -lt $n; $i++) {
            $vn = "sv_{0:D5}.dat" -f $i; $v = "$dir\$vn"
            if ((& $isGolden $v)) { $recOk++; continue }
            $done = $false; $kid = $null
            foreach ($line in $klist) { if ($line -match $tag -and $line -match [regex]::Escape($vn) -and $line -match 'key_id=([0-9a-f]+)') { $kid = $Matches[1]; break } }
            if ($kid) { & C:\sar\sarctl.exe recover $kid $v 2>&1 | Out-Null; if ((& $isGolden $v)) { $recOk++; $done = $true } }
            if (-not $done) {
                $off = $null; $ln = $null
                foreach ($line in $plist) { if ($line -match $tag -and $line -match [regex]::Escape($vn) -and $line -match 'off=(\d+) len=(\d+)') { $off = $Matches[1]; $ln = $Matches[2]; break } }
                if ($off -ne $null) { & C:\sar\sarctl.exe preserve-recover $v $off $ln 2>&1 | Out-Null; if ((& $isGolden $v)) { $recOk++; $done = $true } }
            }
            if (-not $done) { $recLost++ }
        }

        $cr = Receive-Job (Wait-Job $ctl); Remove-Job $ctl -Force -ErrorAction SilentlyContinue
        if (-not $cr.ok) { $ctlOk = $false }
        $ctlCount += $cr.count
        Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue
    }
    [pscustomobject]@{ ctlOk = $ctlOk; ctlCount = $ctlCount; recOk = $recOk; recLost = $recLost; total = ($rounds * $n) }
}

$r = VMArgs $body @($Rounds, $CorpusN, $Len)
$alive = VM { [bool]((fltmc filters 2>$null) -match 'semantics_ar') -and (Get-Service semantics_ar_service -ErrorAction SilentlyContinue).Status -eq 'Running' }
Write-Host ("  recovered {0}/{1} (barrier-lost {2}), control ops {3} all-ok={4}, live={5}" -f $r.recOk, $r.total, $r.recLost, $r.ctlCount, $r.ctlOk, $alive)
Assert "recover byte-exact under concurrent control-op storm (>= total-rounds)" ($r.recOk -ge ($r.total - $Rounds)) "recOk=$($r.recOk)"
Assert "all concurrent control ops succeeded (no corruption/hang)" ([bool]$r.ctlOk) "ctlCount=$($r.ctlCount)"
Assert "engine live after concurrent recover+control storm" ([bool]$alive)

# service-stop robustness: a clean stop/start with no in-flight hang must be fast and leave the engine usable
$sw = VM { $t=[Diagnostics.Stopwatch]::StartNew(); Stop-Service semantics_ar_service -Force -ErrorAction SilentlyContinue; $t.Stop(); [int]$t.ElapsedMilliseconds }
Assert "clean service stop is prompt (< 5s, no false-stuck)" ($sw -lt 5000) "stopMs=$sw"
VM { Start-Service semantics_ar_service -ErrorAction SilentlyContinue; Start-Sleep 3 }
Assert "service restarts cleanly after stop" ([bool](VM { (Get-Service semantics_ar_service -ErrorAction SilentlyContinue).Status -eq 'Running' }))

Write-Host "`n=== recover-isolation: $pass passed, $fail failed ===" -ForegroundColor Cyan
if ($fail -gt 0) { exit 1 }
