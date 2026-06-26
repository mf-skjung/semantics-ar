param(
    [string]$VMName = "SarTarget",
    [string]$Repo = (Join-Path $PSScriptRoot ".."),
    [PSCredential]$Credential
)
$ErrorActionPreference = 'Stop'
$Repo = (Resolve-Path $Repo).Path

if (-not $Credential) {
    $pw = ConvertTo-SecureString "admin" -AsPlainText -Force
    $Credential = New-Object System.Management.Automation.PSCredential("admin", $pw)
}

function VM {
    param([scriptblock]$Script)
    Invoke-Command -VMName $VMName -Credential $Credential -ScriptBlock $Script
}
function VMArgs {
    param([scriptblock]$Script, [object[]]$Arguments)
    Invoke-Command -VMName $VMName -Credential $Credential -ScriptBlock $Script -ArgumentList $Arguments
}
function CopyToVM {
    param([string]$Local, [string]$Remote)
    $s = New-PSSession -VMName $VMName -Credential $Credential
    Copy-Item -Path $Local -Destination $Remote -ToSession $s -Force
    Remove-PSSession $s
}

$pass = 0; $fail = 0; $skip = 0
function Assert {
    param([string]$Name, [bool]$Condition, [string]$Detail = "")
    if ($Condition) {
        Write-Host "  PASS  $Name" -ForegroundColor Green
        $script:pass++
    } else {
        Write-Host "  FAIL  $Name  $Detail" -ForegroundColor Red
        $script:fail++
    }
}
function Skip {
    param([string]$Name, [string]$Reason)
    Write-Host "  SKIP  $Name  ($Reason)" -ForegroundColor Yellow
    $script:skip++
}

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host " semantics-ar Preservation VM Verification" -ForegroundColor Cyan
Write-Host " $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Cyan
Write-Host "========================================`n" -ForegroundColor Cyan

# ── Phase 1: Package & Deploy ──────────────────────────────────────────
Write-Host "Phase 1: Package & Deploy" -ForegroundColor Yellow
Write-Host "--------------------------"

Write-Host "  Checking pre-built driver..."
if (-not (Test-Path "$Repo\build_driver\semantics_ar.sys")) {
    Write-Host "  Building driver..."
    & cmd /c "`"$Repo\scripts\build_driver.bat`"" 2>&1 | Out-Null
    if (-not (Test-Path "$Repo\build_driver\semantics_ar.sys")) {
        throw "Driver build failed"
    }
}

Write-Host "  Packaging driver..."
& "$Repo\scripts\package_driver.ps1" -Out "$Repo\build_driver" -Repo $Repo 2>&1 | Out-Null
$pkg = "$Repo\build_driver\pkg"
if (-not (Test-Path "$pkg\semantics_ar.sys")) {
    throw "Package failed"
}
$certThumb = (Get-ChildItem Cert:\CurrentUser\My |
    Where-Object { $_.Subject -eq 'CN=SemanticsAr Test' } |
    Select-Object -First 1).Thumbprint

Write-Host "  Stopping service on VM..."
try { VM { Stop-Service semantics_ar_service -Force -ErrorAction SilentlyContinue } } catch {}

Write-Host "  Unloading minifilter..."
try { VM { fltmc unload semantics_ar 2>$null } } catch {}
Start-Sleep -Seconds 2

Write-Host "  Clearing store files (clean test state)..."
VM {
    $store = "C:\Windows\System32\drivers\SemanticsAr"
    if (Test-Path $store) {
        Remove-Item "$store\*" -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "  Copying files to VM..."
$vmSar = "C:\sar"
VM { if (-not (Test-Path $using:vmSar)) { New-Item -ItemType Directory -Path $using:vmSar -Force | Out-Null } }

foreach ($f in @("semantics_ar.sys","semantics_ar.inf","semantics_ar.cat","SemanticsArTest.cer")) {
    $src = Join-Path $pkg $f
    if (Test-Path $src) { CopyToVM $src (Join-Path $vmSar $f) }
}

$buildRelease = "$Repo\build\service\Release"
$svcExe = "$buildRelease\semantics_ar_service.exe"
if (Test-Path $svcExe) { CopyToVM $svcExe "$vmSar\semantics_ar_service.exe" }

$sarctlExe = "$Repo\build\tools\Release\sarctl.exe"
if (Test-Path $sarctlExe) { CopyToVM $sarctlExe "$vmSar\sarctl.exe" }

foreach ($h in @("preserve_test.exe","partial_encryptor.exe","ransom_sim.exe","wipe_test.exe","wipe_reuse.exe")) {
    $src = "$Repo\build_harness\$h"
    if (Test-Path $src) { CopyToVM $src "$vmSar\$h" }
}

Write-Host "  Trusting test cert & installing driver..."
VM {
    $cer = "C:\sar\SemanticsArTest.cer"
    if (Test-Path $cer) {
        certutil -addstore Root $cer 2>$null | Out-Null
        certutil -addstore TrustedPublisher $cer 2>$null | Out-Null
    }
    $svc = Get-Service semantics_ar -ErrorAction SilentlyContinue
    if (-not $svc) {
        pnputil /add-driver C:\sar\semantics_ar.inf /install 2>$null | Out-Null
    }
    $imgPath = "\??\C:\sar\semantics_ar.sys"
    Set-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Services\semantics_ar" -Name ImagePath -Value $imgPath -ErrorAction SilentlyContinue
}

Write-Host "  Loading minifilter..."
$loadOut = VM {
    $out = fltmc load semantics_ar 2>&1
    return @{ output = ($out -join " "); exit = $LASTEXITCODE }
}
if ($loadOut.output) { Write-Host "    fltmc: $($loadOut.output)" }
Start-Sleep -Seconds 3

$loaded = VM { (fltmc filters 2>$null) -match 'semantics_ar' }
if (-not $loaded) {
    Write-Host "  Checking ImagePath registry..." -ForegroundColor Yellow
    $regInfo = VM {
        $ip = (Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Services\semantics_ar" -ErrorAction SilentlyContinue).ImagePath
        $exists = Test-Path "C:\sar\semantics_ar.sys"
        return @{ ImagePath = $ip; SysExists = $exists }
    }
    Write-Host "    ImagePath = $($regInfo.ImagePath)"
    Write-Host "    C:\sar\semantics_ar.sys exists = $($regInfo.SysExists)"
}
Assert "minifilter loaded" ([bool]$loaded)

Write-Host "  Starting service..."
VM {
    $svcPath = "C:\sar\semantics_ar_service.exe"
    $svc = Get-Service semantics_ar_service -ErrorAction SilentlyContinue
    if (-not $svc) {
        New-Service -Name semantics_ar_service -BinaryPathName $svcPath -StartupType Manual -ErrorAction SilentlyContinue | Out-Null
    } else {
        sc.exe config semantics_ar_service binPath= $svcPath 2>$null | Out-Null
    }
    Start-Service semantics_ar_service -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 5
}
$svcRunning = VM { (Get-Service semantics_ar_service -ErrorAction SilentlyContinue).Status -eq 'Running' }
if (-not $svcRunning) {
    Write-Host "  Service not running. Checking status..." -ForegroundColor Yellow
    $svcDetail = VM {
        $s = Get-Service semantics_ar_service -ErrorAction SilentlyContinue
        $bp = (Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Services\semantics_ar_service" -ErrorAction SilentlyContinue).ImagePath
        $evts = Get-WinEvent -FilterHashtable @{LogName='System';ProviderName='Service Control Manager';Id=7000,7024;StartTime=(Get-Date).AddMinutes(-5)} -MaxEvents 5 -ErrorAction SilentlyContinue | Select-Object Message
        return @{ Status = $s.Status; BinPath = $bp; Events = ($evts | ForEach-Object { $_.Message }) }
    }
    Write-Host "    Status: $($svcDetail.Status), BinPath: $($svcDetail.BinPath)"
    foreach ($e in $svcDetail.Events) { Write-Host "    Event: $e" }
}
Assert "service running" ([bool]$svcRunning)
if (-not $svcRunning) {
    Write-Host "`n  Service failed to start. Attempting without service (sarctl tests will skip)." -ForegroundColor Yellow
}

# ── Phase 2: Preservation Staging ──────────────────────────────────────
Write-Host "`nPhase 2: Preservation Staging" -ForegroundColor Yellow
Write-Host "------------------------------"

$testDir = "C:\sar\preserve_testdata"
VM {
    $d = "C:\sar\preserve_testdata"
    if (Test-Path $d) { Remove-Item $d -Recurse -Force -Confirm:$false }
    New-Item -ItemType Directory $d -Force | Out-Null
}

Write-Host "  Creating golden test files (known content)..."
$goldenHashes = VM {
    $d = "C:\sar\preserve_testdata"
    $hashes = @{}

    $content1 = [byte[]](1..1024 | ForEach-Object { [byte]($_ % 256) })
    [IO.File]::WriteAllBytes("$d\file_1k.dat", $content1)
    $hashes["file_1k.dat"] = (Get-FileHash "$d\file_1k.dat" -Algorithm SHA256).Hash

    $content2 = [byte[]]::new(4096)
    for ($i = 0; $i -lt 4096; $i++) { $content2[$i] = [byte](($i * 7 + 13) % 256) }
    [IO.File]::WriteAllBytes("$d\file_4k.dat", $content2)
    $hashes["file_4k.dat"] = (Get-FileHash "$d\file_4k.dat" -Algorithm SHA256).Hash

    $content3 = [byte[]]::new(65536)
    for ($i = 0; $i -lt 65536; $i++) { $content3[$i] = [byte](($i * 11 + 37) % 256) }
    [IO.File]::WriteAllBytes("$d\file_64k.dat", $content3)
    $hashes["file_64k.dat"] = (Get-FileHash "$d\file_64k.dat" -Algorithm SHA256).Hash

    $content4 = [byte[]]::new(32)
    for ($i = 0; $i -lt 32; $i++) { $content4[$i] = [byte](0xAA) }
    [IO.File]::WriteAllBytes("$d\file_32b.dat", $content4)
    $hashes["file_32b.dat"] = (Get-FileHash "$d\file_32b.dat" -Algorithm SHA256).Hash

    $content5 = [byte[]]::new(16)
    for ($i = 0; $i -lt 16; $i++) { $content5[$i] = [byte](0xBB) }
    [IO.File]::WriteAllBytes("$d\file_16b.dat", $content5)
    $hashes["file_16b.dat"] = (Get-FileHash "$d\file_16b.dat" -Algorithm SHA256).Hash

    $content6 = [byte[]]::new(262144)
    for ($i = 0; $i -lt 262144; $i++) { $content6[$i] = [byte](($i * 5 + 19) % 256) }
    [IO.File]::WriteAllBytes("$d\file_256k.dat", $content6)
    $hashes["file_256k.dat"] = (Get-FileHash "$d\file_256k.dat" -Algorithm SHA256).Hash

    $content7 = [byte[]]::new(524288)
    for ($i = 0; $i -lt 524288; $i++) { $content7[$i] = [byte](($i * 9 + 41) % 256) }
    [IO.File]::WriteAllBytes("$d\file_512k.dat", $content7)
    $hashes["file_512k.dat"] = (Get-FileHash "$d\file_512k.dat" -Algorithm SHA256).Hash

    $content8 = [byte[]]::new(1048576)
    for ($i = 0; $i -lt 1048576; $i++) { $content8[$i] = [byte](($i * 3 + 5) % 256) }
    [IO.File]::WriteAllBytes("$d\file_1m.dat", $content8)
    $hashes["file_1m.dat"] = (Get-FileHash "$d\file_1m.dat" -Algorithm SHA256).Hash

    return $hashes
}
Write-Host "  Golden hashes captured for 8 files (16B, 32B, 1KB, 4KB, 64KB, 256KB, 512KB, 1MB)"
foreach ($k in $goldenHashes.Keys | Sort-Object) {
    Write-Host "    $k = $($goldenHashes[$k].Substring(0,16))..."
}

Write-Host "  Checking preserve-list baseline (should be empty or from prior run)..."
$baselineCount = VM {
    $out = & C:\sar\sarctl.exe preserve-list 2>&1
    $m = $out | Select-String '(\d+) preserved region'
    if ($m) { [int]$m.Matches[0].Groups[1].Value } else { -1 }
}
Write-Host "  Baseline preserve entries: $baselineCount"

Write-Host "  Running preserve_test (per-file random AES-256-CBC, uncatchable)..."
$encResult = VM {
    & C:\sar\preserve_test.exe `
        C:\sar\preserve_testdata\file_1k.dat `
        C:\sar\preserve_testdata\file_4k.dat `
        C:\sar\preserve_testdata\file_64k.dat `
        C:\sar\preserve_testdata\file_32b.dat `
        C:\sar\preserve_testdata\file_16b.dat 2>&1
    $LASTEXITCODE
}
Write-Host "  preserve_test output:"
foreach ($line in $encResult) { Write-Host "    $line" }

Write-Host "  Waiting for deferred workers to complete..."
Start-Sleep -Seconds 5

Write-Host "  Checking preserve-list after encryption..."
$postEncList = VM {
    $out = & C:\sar\sarctl.exe preserve-list 2>&1
    $out
}
Write-Host "  preserve-list output:"
foreach ($line in $postEncList) { Write-Host "    $line" }

$postCount = 0
$countLine = $postEncList | Select-String '(\d+) preserved region'
if ($countLine) { $postCount = [int]$countLine.Matches[0].Groups[1].Value }
$newEntries = $postCount - [Math]::Max(0, $baselineCount)
Assert "preservation staged entries" ($newEntries -gt 0) "expected >0, got $newEntries"
Write-Host "  New preserved regions: $newEntries (race with cache flush may prevent some)"

$postEncHashes = VM {
    $d = "C:\sar\preserve_testdata"
    $h = @{}
    foreach ($f in @("file_1k.dat","file_4k.dat","file_64k.dat","file_32b.dat","file_16b.dat")) {
        $h[$f] = (Get-FileHash "$d\$f" -Algorithm SHA256).Hash
    }
    return $h
}
$encryptedCount = 0
foreach ($f in @("file_1k.dat","file_4k.dat","file_64k.dat","file_32b.dat","file_16b.dat")) {
    if ($postEncHashes[$f] -ne $goldenHashes[$f]) { $encryptedCount++ }
}
Assert "files actually encrypted" ($encryptedCount -ge 4) "expected >=4 encrypted, got $encryptedCount"

# ── Phase 3: First-Write-Wins ──────────────────────────────────────────
Write-Host "`nPhase 3: First-Write-Wins" -ForegroundColor Yellow
Write-Host "--------------------------"

Write-Host "  Re-encrypting already-encrypted files (second write to same region)..."
$reEncResult = VM {
    & C:\sar\preserve_test.exe `
        C:\sar\preserve_testdata\file_1k.dat `
        C:\sar\preserve_testdata\file_4k.dat 2>&1
    $LASTEXITCODE
}
Start-Sleep -Seconds 5

$postReEncList = VM { & C:\sar\sarctl.exe preserve-list 2>&1 }
$file1kCount = ($postReEncList | Where-Object { $_ -match 'file_1k\.dat' }).Count
$file4kCount = ($postReEncList | Where-Object { $_ -match 'file_4k\.dat' }).Count
Write-Host "  file_1k.dat entries: $file1kCount, file_4k.dat entries: $file4kCount"
$postReEncTotal = 0
$countLine = $postReEncList | Select-String '(\d+) preserved region'
if ($countLine) { $postReEncTotal = [int]$countLine.Matches[0].Groups[1].Value }
if ($postReEncTotal -gt $postCount) {
    Write-Host "  Total entries grew ($postCount -> $postReEncTotal) — checking for non-test entries:"
    foreach ($line in $postReEncList) {
        if ($line -match '^\[' -and $line -notmatch 'preserve_testdata') {
            Write-Host "    system file: $line"
        }
    }
}
Assert "first-write-wins (no duplicate entries per file)" ($file1kCount -le 1 -and $file4kCount -le 1) `
    "file_1k=$file1kCount file_4k=$file4kCount (expected 1 each)"

# ── Phase 4: Preservation Restore Round-trip ───────────────────────────
Write-Host "`nPhase 4: Preservation Restore Round-trip" -ForegroundColor Yellow
Write-Host "------------------------------------------"

if ($newEntries -gt 0) {
    $restoreResults = VM {
        $results = @()
        $list = & C:\sar\sarctl.exe preserve-list 2>&1
        foreach ($line in $list) {
            if ($line -match '^\[(\d+)\]\s+off=(\d+)\s+len=(\d+)\s+size=\d+\s+(.+)$') {
                $off = $Matches[2]
                $len = $Matches[3]
                $ntpath = $Matches[4].Trim()

                $dosPath = $null
                if ($ntpath -match '\\Device\\HarddiskVolume\d+\\(.+)') {
                    $dosPath = "C:\$($Matches[1])"
                }
                if (-not $dosPath) { continue }
                if ($dosPath -notmatch 'preserve_testdata') { continue }

                $preHash = (Get-FileHash $dosPath -Algorithm SHA256).Hash

                $recOut = & C:\sar\sarctl.exe preserve-recover $dosPath $off $len 2>&1
                $recStatus = $recOut -join " "
                $recOk = $recStatus -match 'result=0'

                $postHash = $null
                if ($recOk) {
                    $postHash = (Get-FileHash $dosPath -Algorithm SHA256).Hash
                }

                $results += [PSCustomObject]@{
                    Path = $dosPath
                    Offset = $off
                    Length = $len
                    PreHash = $preHash
                    PostHash = $postHash
                    RecoverOk = $recOk
                    RecoverOutput = $recStatus
                }
            }
        }
        return $results
    }

    $restoredToGolden = 0
    foreach ($r in $restoreResults) {
        $fname = Split-Path $r.Path -Leaf
        $golden = $goldenHashes[$fname]
        $match = ($r.PostHash -eq $golden)
        if ($match) { $restoredToGolden++ }
        Write-Host "    $fname : recover=$($r.RecoverOk), SHA-256 match golden=$match"
        if (-not $r.RecoverOk) { Write-Host "      output: $($r.RecoverOutput)" }
    }
    Assert "at least one file restored to golden SHA-256" ($restoredToGolden -gt 0) `
        "restored $restoredToGolden / $($restoreResults.Count)"
} else {
    Skip "restore round-trip" "no preserved regions available"
}

# ── Phase 5: Oracle Reconciliation (III.5.4 containment) ───────────────
Write-Host "`nPhase 5: Oracle->Preservation Reconciliation" -ForegroundColor Yellow
Write-Host "----------------------------------------------"

Write-Host "  Creating reconciliation test file (fixed-key, Oracle-catchable)..."
VM {
    $d = "C:\sar\preserve_testdata"
    $recon = [byte[]]::new(8192)
    for ($i = 0; $i -lt 8192; $i++) { $recon[$i] = [byte](($i * 13 + 7) % 256) }
    [IO.File]::WriteAllBytes("$d\recon_oracle.dat", $recon)
}
$reconGolden = VM { (Get-FileHash "C:\sar\preserve_testdata\recon_oracle.dat" -Algorithm SHA256).Hash }
Write-Host "  Golden: $($reconGolden.Substring(0,16))..."

$preReconCount = VM {
    $out = & C:\sar\sarctl.exe preserve-list 2>&1
    $m = $out | Select-String '(\d+) preserved region'
    if ($m) { [int]$m.Matches[0].Groups[1].Value } else { 0 }
}

Write-Host "  Running partial_encryptor (fixed key 0xC1A5..., 12s hold for Oracle)..."
$partialOut = VM {
    & C:\sar\partial_encryptor.exe C:\sar\preserve_testdata\recon_oracle.dat 2>&1
}
foreach ($line in $partialOut) { Write-Host "    $line" }

Write-Host "  Waiting for Oracle conviction + reconciliation (15s)..."
Start-Sleep -Seconds 15

$keystoreOut = VM {
    & C:\sar\sarctl.exe list 2>&1
}
Write-Host "  Keystore entries:"
foreach ($line in $keystoreOut) { Write-Host "    $line" }

$postReconPreserve = VM {
    $out = & C:\sar\sarctl.exe preserve-list 2>&1
    $out
}
Write-Host "  preserve-list after Oracle conviction:"
foreach ($line in $postReconPreserve) { Write-Host "    $line" }

$postReconCount = 0
$cline = $postReconPreserve | Select-String '(\d+) preserved region'
if ($cline) { $postReconCount = [int]$cline.Matches[0].Groups[1].Value }

$reconEntryGone = $true
foreach ($line in $postReconPreserve) {
    if ($line -match 'recon_oracle') { $reconEntryGone = $false; break }
}
if ($preReconCount -gt 0 -or $newEntries -gt 0) {
    Assert "reconciliation removed Oracle-convicted file from preserve" $reconEntryGone `
        "recon_oracle.dat should be removed by reconciliation since Oracle now holds the key"
} else {
    Skip "reconciliation" "no prior preserve entries to compare"
}

$reconRecovered = VM {
    $klist = & C:\sar\sarctl.exe list 2>&1
    foreach ($line in $klist) {
        if ($line -match 'recon_oracle') {
            if ($line -match 'key_id=([0-9a-f]+)') {
                $keyId = $Matches[1]
                $recOut = & C:\sar\sarctl.exe recover $keyId C:\sar\preserve_testdata\recon_oracle.dat 2>&1
                return ($recOut -join " ")
            }
        }
    }
    return "no keystore entry for recon_oracle"
}
Write-Host "  Oracle recovery attempt: $reconRecovered"

$reconHash = VM { (Get-FileHash "C:\sar\preserve_testdata\recon_oracle.dat" -Algorithm SHA256).Hash }
Assert "Oracle recovery restores to golden" ($reconHash -eq $reconGolden) `
    "post=$($reconHash.Substring(0,16))... expected=$($reconGolden.Substring(0,16))..."

# ── Phase 6: Large File Preservation (256KB, 512KB, 1MB) ───────────────
Write-Host "`nPhase 6: Large File Preservation (256KB, 512KB, 1MB)" -ForegroundColor Yellow
Write-Host "------------------------------------------------------"

Write-Host "  Running preserve_test on all large files (single batch, 500ms spacing)..."
$lgResult = VM {
    & C:\sar\preserve_test.exe `
        C:\sar\preserve_testdata\file_256k.dat `
        C:\sar\preserve_testdata\file_512k.dat `
        C:\sar\preserve_testdata\file_1m.dat 2>&1
}
foreach ($line in $lgResult) { Write-Host "    $line" }

Write-Host "  Polling for staging (up to 45s)..."
$lgList = $null
$lgDeadline = (Get-Date).AddSeconds(45)
do {
    Start-Sleep -Seconds 3
    $lgList = VM { & C:\sar\sarctl.exe preserve-list 2>&1 }
    $lgJoined = $lgList -join "`n"
    $allFound = ($lgJoined -match 'file_256k') -and ($lgJoined -match 'file_512k') -and ($lgJoined -match 'file_1m')
    if ($allFound) { break }
} while ((Get-Date) -lt $lgDeadline)
Write-Host "  preserve-list:"
foreach ($line in $lgList) { Write-Host "    $line" }

$lgAllPassed = $true
foreach ($sz in @(
    @{ name = "file_256k.dat"; label = "256KB"; size = 262144 },
    @{ name = "file_512k.dat"; label = "512KB"; size = 524288 },
    @{ name = "file_1m.dat";   label = "1MB";   size = 1048576 }
)) {
    $fname = $sz.name
    $golden = $goldenHashes[$fname]

    $lgHash = VMArgs { param($f) (Get-FileHash $f -Algorithm SHA256).Hash } @("C:\sar\preserve_testdata\$fname")
    $modified = $lgHash -ne $golden
    $staged = ($lgList -join "`n") -match [regex]::Escape($fname)
    Write-Host "  [$($sz.label)] modified=$modified staged=$staged"

    if ($staged) {
        $lgRestore = VMArgs { param($f, $fn)
            $list = & C:\sar\sarctl.exe preserve-list 2>&1
            foreach ($line in $list) {
                if ($line -match [regex]::Escape($fn) -and $line -match '^\[(\d+)\]\s+off=(\d+)\s+len=(\d+)') {
                    $off = $Matches[2]; $len = $Matches[3]
                    & C:\sar\sarctl.exe preserve-recover $f $off $len 2>&1 | Out-Null
                    return (Get-FileHash $f -Algorithm SHA256).Hash
                }
            }
            return "no-match"
        } @("C:\sar\preserve_testdata\$fname", $fname)
        Assert "$($sz.label) file stage+restore" ($lgRestore -eq $golden) `
            "got=$($lgRestore.Substring(0,16))..."
    } else {
        $lgAllPassed = $false
        Assert "$($sz.label) file staged (within STAGE_MAX)" $false `
            "$($sz.label) < 4MB stage max, modified=$modified"
    }
}
if (-not $lgAllPassed) {
    Write-Host "  Keystore check for Oracle conviction of unstaged files:"
    $lgKs = VM { & C:\sar\sarctl.exe list 2>&1 }
    foreach ($line in $lgKs) { Write-Host "    $line" }
}

# ── Phase 7: Self-Protection ──────────────────────────────────────────
Write-Host "`nPhase 7: Self-Protection (preserve store files)" -ForegroundColor Yellow
Write-Host "-------------------------------------------------"

$storeDir = "C:\Windows\System32\drivers\SemanticsAr"
$selfProtResult = VM {
    $results = @{}
    $dir = "C:\Windows\System32\drivers\SemanticsAr"
    if (Test-Path $dir) {
        $files = Get-ChildItem $dir -ErrorAction SilentlyContinue
        foreach ($f in $files) {
            try {
                Remove-Item $f.FullName -Force -Confirm:$false -ErrorAction Stop
                $results[$f.Name] = "DELETED (FAIL)"
            } catch {
                $results[$f.Name] = "PROTECTED (OK)"
            }
        }
        if ($files.Count -eq 0) { $results["_status"] = "directory exists but empty" }
    } else {
        $results["_status"] = "directory not found"
    }
    return $results
}
Write-Host "  Store file protection:"
$allProtected = $true
foreach ($k in $selfProtResult.Keys | Sort-Object) {
    $v = $selfProtResult[$k]
    Write-Host "    $k : $v"
    if ($v -like "*DELETED*") { $allProtected = $false }
}
if ($selfProtResult.ContainsKey("_status")) {
    Skip "self-protection" $selfProtResult["_status"]
} else {
    Assert "all store files protected from user-mode deletion" $allProtected
}

# ── Phase 8: Budget Control ──────────────────────────────────────────
Write-Host "`nPhase 8: Budget Control (sarctl budget)" -ForegroundColor Yellow
Write-Host "-----------------------------------------"

$budgetResult = VM {
    & C:\sar\sarctl.exe budget 3600 5 2>&1
}
$budgetOk = ($budgetResult -join " ") -match 'result=0'
Assert "budget set (1 hour retention, 5 MB capacity)" $budgetOk "output: $($budgetResult -join ' ')"

$budgetReset = VM {
    & C:\sar\sarctl.exe budget 604800 10240 2>&1
}
$resetOk = ($budgetReset -join " ") -match 'result=0'
Assert "budget restored to defaults (7 days, 10 GB)" $resetOk

# ── Phase 9: ENFORCE Capacity Exhaustion Block ────────────────────────
Write-Host "`nPhase 9: ENFORCE Capacity Exhaustion Block" -ForegroundColor Yellow
Write-Host "--------------------------------------------"

Write-Host "  Setting budget (1 day retention, 1 MB capacity)..."
VM {
    & C:\sar\sarctl.exe budget 86400 1 2>&1
}

Write-Host "  Switching to ENFORCE mode..."
$modeResult = VM {
    & C:\sar\sarctl.exe mode enforce 2>&1
}
$enforceOk = ($modeResult -join " ") -match 'result=0'
Assert "ENFORCE mode activated" $enforceOk "output: $($modeResult -join ' ')"

Write-Host "  Creating overflow files to exceed 1 MB budget..."
VM {
    $d = "C:\sar\preserve_testdata"
    for ($i = 0; $i -lt 20; $i++) {
        $content = [byte[]]::new(131072)
        for ($j = 0; $j -lt 131072; $j++) { $content[$j] = [byte](($i * 17 + $j) % 256) }
        [IO.File]::WriteAllBytes("$d\overflow_$i.dat", $content)
    }
}

Write-Host "  preserve-list before overflow test:"
$preOverflow = VM { & C:\sar\sarctl.exe preserve-list 2>&1 }
foreach ($line in $preOverflow) { Write-Host "    $line" }

Write-Host "  Running preserve_test on overflow files (single process, 500ms inter-file delay)..."
$overflowResult = VM {
    $files = @()
    for ($i = 0; $i -lt 20; $i++) { $files += "C:\sar\preserve_testdata\overflow_$i.dat" }
    $out = & C:\sar\preserve_test.exe @files 2>&1
    $exitCode = $LASTEXITCODE
    return @{ output = ($out -join "`n"); exit = $exitCode }
}
Write-Host "  overflow result (exit=$($overflowResult.exit)):"
foreach ($line in $overflowResult.output -split "`n") { Write-Host "    $line" }
Start-Sleep -Seconds 5

Write-Host "  preserve-list after overflow test:"
$postOverflow = VM { & C:\sar\sarctl.exe preserve-list 2>&1 }
foreach ($line in $postOverflow) { Write-Host "    $line" }

$overflowPostHashes = VM {
    $d = "C:\sar\preserve_testdata"
    $changed = 0; $unchanged = 0
    for ($i = 0; $i -lt 20; $i++) {
        $orig = [byte[]]::new(131072)
        for ($j = 0; $j -lt 131072; $j++) { $orig[$j] = [byte](($i * 17 + $j) % 256) }
        $current = [IO.File]::ReadAllBytes("$d\overflow_$i.dat")
        $same = $true
        if ($current.Length -eq $orig.Length) {
            for ($j = 0; $j -lt $current.Length; $j++) {
                if ($current[$j] -ne $orig[$j]) { $same = $false; break }
            }
        } else { $same = $false }
        if ($same) { $unchanged++ } else { $changed++ }
    }
    return @{ changed = $changed; unchanged = $unchanged }
}
Write-Host "  Overflow files: changed=$($overflowPostHashes.changed), unchanged=$($overflowPostHashes.unchanged)"
if ($overflowPostHashes.unchanged -gt 0) {
    Assert "ENFORCE blocked writes after capacity exhaustion" $true `
        "$($overflowPostHashes.unchanged) / 20 writes blocked"
} else {
    Assert "ENFORCE capacity blocking" $false `
        "all 20 files were modified — expected some to be blocked"
}

Write-Host "  Restoring AUDIT mode and default budget..."
VM {
    & C:\sar\sarctl.exe mode audit 2>&1 | Out-Null
    & C:\sar\sarctl.exe budget 604800 10240 2>&1 | Out-Null
}

# ── Phase 10: Edge Cases ──────────────────────────────────────────────
Write-Host "`nPhase 10: Edge Cases" -ForegroundColor Yellow
Write-Host "---------------------"

Write-Host "  Test: file smaller than SAR_CANDIDATE_SIZE (15 bytes)..."
VM {
    $tiny = [byte[]](1..15)
    [IO.File]::WriteAllBytes("C:\sar\preserve_testdata\tiny_15b.dat", $tiny)
}
$tinyResult = VM {
    & C:\sar\preserve_test.exe C:\sar\preserve_testdata\tiny_15b.dat 2>&1
}
foreach ($line in $tinyResult) { Write-Host "    $line" }
Start-Sleep -Seconds 3
$tinyPreserved = VM {
    $out = & C:\sar\sarctl.exe preserve-list 2>&1
    ($out -join "`n") -match 'tiny_15b'
}
Assert "sub-SAR_CANDIDATE_SIZE file correctly skipped" (-not $tinyPreserved) `
    "15-byte file should not reach Gate (requires >= 16 bytes)"

Write-Host "  Test: already-encrypted file (double-encrypt identity check)..."
$doubleResult = VM {
    $d = "C:\sar\preserve_testdata"
    $content = [byte[]]::new(512)
    for ($i = 0; $i -lt 512; $i++) { $content[$i] = [byte](0xDD) }
    [IO.File]::WriteAllBytes("$d\double_test.dat", $content)
    & C:\sar\preserve_test.exe "$d\double_test.dat" 2>&1 | Out-Null
    Start-Sleep -Seconds 2
    $h1 = (Get-FileHash "$d\double_test.dat" -Algorithm SHA256).Hash
    & C:\sar\preserve_test.exe "$d\double_test.dat" 2>&1 | Out-Null
    Start-Sleep -Seconds 2
    $h2 = (Get-FileHash "$d\double_test.dat" -Algorithm SHA256).Hash
    return @{ afterFirst = $h1; afterSecond = $h2; different = ($h1 -ne $h2) }
}
Assert "double-encryption produces different ciphertext (unique random keys)" $doubleResult.different

# ── Phase 11: Persist across Reboot ──────────────────────────────────
Write-Host "`nPhase 11: Persist across Reboot" -ForegroundColor Yellow
Write-Host "--------------------------------"

Write-Host "  Creating reboot-test file..."
$rebootGolden = VM {
    $d = "C:\sar\preserve_testdata"
    $content = [byte[]]::new(8192)
    for ($i = 0; $i -lt 8192; $i++) { $content[$i] = [byte](($i * 13 + 77) % 256) }
    [IO.File]::WriteAllBytes("$d\reboot_test.dat", $content)
    return (Get-FileHash "$d\reboot_test.dat" -Algorithm SHA256).Hash
}
Write-Host "  Golden: $($rebootGolden.Substring(0,16))..."

Write-Host "  Encrypting reboot-test file..."
VM { & C:\sar\preserve_test.exe C:\sar\preserve_testdata\reboot_test.dat 2>&1 | Out-Null }

Write-Host "  Polling for reboot_test staging (up to 45s)..."
$hasRebootEntry = $false
$preRebootList = $null
$rbDeadline = (Get-Date).AddSeconds(45)
do {
    Start-Sleep -Seconds 3
    $preRebootList = VM { & C:\sar\sarctl.exe preserve-list 2>&1 }
    $hasRebootEntry = ($preRebootList -join "`n") -match 'reboot_test'
    if ($hasRebootEntry) { break }
} while ((Get-Date) -lt $rbDeadline)

$preRebootCount = 0
$countLine = $preRebootList | Select-String '(\d+) preserved region'
if ($countLine) { $preRebootCount = [int]$countLine.Matches[0].Groups[1].Value }
Write-Host "  Pre-reboot: $preRebootCount entries, reboot_test staged=$hasRebootEntry"

if (-not $hasRebootEntry) {
    Skip "persist across reboot" "reboot_test.dat not staged after 45s — cannot test persist"
} else {
    Write-Host "  Waiting for persist thread flush (10s)..."
    Start-Sleep -Seconds 10

    Write-Host "  Restarting VM..."
    Restart-VM -Name $VMName -Force -Wait
    $ready = $false
    for ($attempt = 0; $attempt -lt 60; $attempt++) {
        Start-Sleep -Seconds 5
        try {
            Invoke-Command -VMName $VMName -Credential $Credential -ScriptBlock { 1 } -ErrorAction Stop | Out-Null
            $ready = $true
            break
        } catch {}
    }
    if (-not $ready) {
        Write-Host "  VM failed to come back after reboot!" -ForegroundColor Red
        $fail++
    } else {
        Write-Host "  VM back after reboot. Loading minifilter..."
        VM { fltmc load semantics_ar 2>$null | Out-Null }
        Start-Sleep -Seconds 3
        $loaded = VM { (fltmc filters 2>$null) -match 'semantics_ar' }
        Assert "minifilter loaded after reboot" ([bool]$loaded)

        Write-Host "  Starting service..."
        VM { Start-Service semantics_ar_service -ErrorAction SilentlyContinue; Start-Sleep -Seconds 5 }
        $svcRunning = VM { (Get-Service semantics_ar_service -ErrorAction SilentlyContinue).Status -eq 'Running' }
        Assert "service running after reboot" ([bool]$svcRunning)

        $postRebootList = VM { & C:\sar\sarctl.exe preserve-list 2>&1 }
        Write-Host "  Post-reboot preserve-list:"
        foreach ($line in $postRebootList) { Write-Host "    $line" }
        $postRebootCount = 0
        $countLine = $postRebootList | Select-String '(\d+) preserved region'
        if ($countLine) { $postRebootCount = [int]$countLine.Matches[0].Groups[1].Value }
        Assert "preserve entries survived reboot" ($postRebootCount -ge $preRebootCount) `
            "pre=$preRebootCount post=$postRebootCount"

        $postRebootHasEntry = ($postRebootList -join "`n") -match 'reboot_test'
        Assert "reboot_test entry persisted" ([bool]$postRebootHasEntry)

        if ($postRebootHasEntry) {
            Write-Host "  Restoring reboot_test.dat..."
            $restoreHash = VM {
                $list = & C:\sar\sarctl.exe preserve-list 2>&1
                foreach ($line in $list) {
                    if ($line -match 'reboot_test' -and $line -match '^\[(\d+)\]\s+off=(\d+)\s+len=(\d+)') {
                        $off = $Matches[2]; $len = $Matches[3]
                        & C:\sar\sarctl.exe preserve-recover C:\sar\preserve_testdata\reboot_test.dat $off $len 2>&1 | Out-Null
                        return (Get-FileHash "C:\sar\preserve_testdata\reboot_test.dat" -Algorithm SHA256).Hash
                    }
                }
                return "no-match"
            }
            Assert "reboot_test restore matches golden" ($restoreHash -eq $rebootGolden) `
                "got=$($restoreHash.Substring(0,16))..."
        }

        Write-Host "  Testing new staging after reboot..."
        $postRebootStage = VM {
            $d = "C:\sar\preserve_testdata"
            $content = [byte[]]::new(4096)
            for ($i = 0; $i -lt 4096; $i++) { $content[$i] = [byte](($i * 23 + 99) % 256) }
            [IO.File]::WriteAllBytes("$d\post_reboot.dat", $content)
            & C:\sar\preserve_test.exe "$d\post_reboot.dat" 2>&1 | Out-Null
            Start-Sleep -Seconds 10
            $list = & C:\sar\sarctl.exe preserve-list 2>&1
            return ($list -join "`n") -match 'post_reboot'
        }
        Assert "new file staged after reboot" ([bool]$postRebootStage)
    }
}

# ── Summary ───────────────────────────────────────────────────────────
Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host " RESULTS: $pass passed, $fail failed, $skip skipped" -ForegroundColor $(if ($fail -eq 0) { 'Green' } else { 'Red' })
Write-Host "========================================`n" -ForegroundColor Cyan

if ($fail -gt 0) {
    Write-Host "FAILED assertions require investigation." -ForegroundColor Red
    exit 1
}
