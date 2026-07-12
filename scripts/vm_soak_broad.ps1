param(
    [string]$VMName = "SarTarget",
    [PSCredential]$Credential,
    [int]$Rounds = 6
)
# Broad cross-path concurrency soak. The functional suite exercises capture / mmap / recover / control
# SEPARATELY; this drives them SIMULTANEOUSLY and sustained, watching for a wedge, a service hang, or a
# resource leak - the class of latent concurrency defect that produced the original kernel wedge. Runs
# against the already-deployed VM (driver + the hardened service). ENFORCE mode = block-before-evict, so
# destructive writes are refused (files stay intact) and the invariant under stress is: engine stays live
# and the control channel stays responsive throughout, with no handle/thread growth in the service.
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

Write-Host "`n=== broad cross-path concurrency soak ($(Get-Date -Format 'HH:mm:ss')) ===" -ForegroundColor Cyan
Connect-VM
Assert "minifilter live" ([bool](VM { [bool]((fltmc filters 2>$null) -match 'semantics_ar') }))
Assert "service running" ([bool](VM { (Get-Service semantics_ar_service -ErrorAction SilentlyContinue).Status -eq 'Running' }))
$svcPid0 = VM { (Get-Process semantics_ar_service -ErrorAction SilentlyContinue).Id }
$h0 = VM { (Get-Process semantics_ar_service -ErrorAction SilentlyContinue).HandleCount }
$t0 = VM { (Get-Process semantics_ar_service -ErrorAction SilentlyContinue).Threads.Count }
VM { & C:\sar\sarctl.exe mode enforce 2>$null | Out-Null }

$body = {
    param($round)
    $sd = "C:\sar\broad_s_$round"; $md = "C:\sar\broad_m_$round"
    foreach ($d in @($sd, $md)) { New-Item -ItemType Directory $d -Force | Out-Null; for ($i = 0; $i -lt 24; $i++) { $b = [byte[]]::new(4096); for ($k=0;$k -lt 4096;$k++){$b[$k]=[byte](65+($k%26))}; [IO.File]::WriteAllBytes(("{0}\sv_{1:D5}.dat" -f $d,$i), $b) } }

    # concurrent storms: capture (stream_transform), mmap oversize (mmap_over), control ops
    $jCap = Start-Job -ScriptBlock { param($d) & C:\sar\stream_transform.exe chacha 20 resident $d 24 4 2>&1 | Out-Null } -ArgumentList $sd
    $jMmap = if (Test-Path C:\sar\mmap_over.exe) { Start-Job -ScriptBlock { param($d) & C:\sar\mmap_over.exe $d\sv_00000.dat $d\sv_00001.dat 2>&1 | Out-Null } -ArgumentList $md } else { $null }
    $jCtl = Start-Job -ScriptBlock {
        $ok = $true
        for ($j = 0; $j -lt 60; $j++) {
            if ((& C:\sar\sarctl.exe status 2>&1) -notmatch 'mode|drv|svc') { $ok = $false }
            & C:\sar\sarctl.exe preserve-list 2>&1 | Out-Null; if ($LASTEXITCODE -ne 0) { $ok = $false }
            & C:\sar\sarctl.exe list 2>&1 | Out-Null; if ($LASTEXITCODE -ne 0) { $ok = $false }
        }
        $ok
    }

    # foreground: recover storm over whatever got preserved
    Start-Sleep -Milliseconds 500
    for ($rep = 0; $rep -lt 3; $rep++) {
        foreach ($line in (& C:\sar\sarctl.exe preserve-list 2>&1)) {
            if ($line -match 'off=(\d+) len=(\d+).*  (\S.*)$') {
                & C:\sar\sarctl.exe preserve-recover $Matches[3].Trim() $Matches[1] $Matches[2] 2>&1 | Out-Null
            }
        }
    }

    $jobs = @($jCap, $jCtl) + (@($jMmap) | Where-Object { $_ })
    $ctlOk = $true
    foreach ($jj in $jobs) { $rv = Receive-Job (Wait-Job $jj -Timeout 60); if ($jj -eq $jCtl -and -not $rv) { $ctlOk = $false }; Remove-Job $jj -Force -ErrorAction SilentlyContinue }
    Remove-Item $sd, $md -Recurse -Force -ErrorAction SilentlyContinue
    [pscustomobject]@{ ctlOk = $ctlOk }
}

for ($r = 1; $r -le $Rounds; $r++) {
    $res = VMArgs $body @($r)
    $live = VM { [bool]((fltmc filters 2>$null) -match 'semantics_ar') }
    $svcUp = VM { (Get-Service semantics_ar_service -ErrorAction SilentlyContinue).Status -eq 'Running' }
    $samePid = [bool]((VM { (Get-Process semantics_ar_service -ErrorAction SilentlyContinue).Id }) -eq $svcPid0)
    Write-Host ("  round {0}/{1}: filter-live={2} svc-up={3} same-pid(no-crash)={4} ctlOk={5}" -f $r, $Rounds, $live, $svcUp, $samePid, $res.ctlOk)
    Assert "round $r minifilter live"        ([bool]$live)
    Assert "round $r service up + same PID (no crash/restart)" ([bool]($svcUp -and $samePid))
    Assert "round $r control channel responsive" ([bool]$res.ctlOk)
}

# leak check: handle/thread counts should not have grown materially over the soak
$h1 = VM { (Get-Process semantics_ar_service -ErrorAction SilentlyContinue).HandleCount }
$t1 = VM { (Get-Process semantics_ar_service -ErrorAction SilentlyContinue).Threads.Count }
Write-Host ("  service handles {0}->{1}, threads {2}->{3}" -f $h0, $h1, $t0, $t1)
Assert "no handle leak (growth < 200)" ([bool](($h1 - $h0) -lt 200)) "delta=$($h1-$h0)"
Assert "no thread leak (growth < 20)"  ([bool](($t1 - $t0) -lt 20)) "delta=$($t1-$t0)"

VM { & C:\sar\sarctl.exe mode audit 2>$null | Out-Null }
Write-Host "`n=== broad soak: $pass passed, $fail failed ===" -ForegroundColor Cyan
if ($fail -gt 0) { exit 1 }
