param(
    [string]$VMName = "SarTarget",
    [string]$Repo = (Join-Path $PSScriptRoot ".."),
    [PSCredential]$Credential,
    [switch]$SkipRestore
)
$ErrorActionPreference = 'Stop'
$Repo = (Resolve-Path $Repo).Path
$Evid = Join-Path $Repo "build_verify"
if (-not (Test-Path $Evid)) { New-Item -ItemType Directory -Path $Evid -Force | Out-Null }
$stamp = "20260707"

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
    throw "Cannot open PS Direct session."
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

if (-not $SkipRestore) {
    Write-Host "Restore: clean-baseline-20260704" -ForegroundColor Yellow
    Stop-VM -Name $VMName -TurnOff -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 6
    Restore-VMSnapshot -VMName $VMName -Name "clean-baseline-20260704" -Confirm:$false
    Start-VM -Name $VMName
    Start-Sleep -Seconds 45
    $script:Sess = $null; Connect-VM
    Write-Host "  PS Direct after restore: $(VM { $env:COMPUTERNAME })"
}

Write-Host "Deploy instrumented driver (eager OFF, capture-fingerprint)" -ForegroundColor Yellow
$pkg = "$Repo\build_driver\pkg"
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
CopyToVM "$Repo\build_harness\mmap_probe.exe" "C:\sar\mmap_probe.exe"
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
if (-not (VM { [bool]((fltmc filters 2>$null) -match 'semantics_ar') })) { throw "load failed: $loadOut" }
VM {
    $p="C:\sar\semantics_ar_service.exe"
    if (-not (Get-Service semantics_ar_service -ErrorAction SilentlyContinue)) { New-Service -Name semantics_ar_service -BinaryPathName $p -StartupType Manual -ErrorAction SilentlyContinue | Out-Null }
    Start-Service semantics_ar_service -ErrorAction SilentlyContinue; Start-Sleep -Seconds 4
}
Write-Host "  live: $(VM { [bool]((fltmc filters 2>$null) -match 'semantics_ar') })"

foreach ($k in 1..3) { VM { & C:\sar\sarctl.exe budget 3600 512 2>&1 | Out-Null }; Start-Sleep -Milliseconds 800 }

# Known pre-image: fill 4MB file with 0xAA. mmap dirties every 16th page via XOR 0xFF -> 0x55.
# Capture reads on-disk pre-image; correct capture = 0xAA, stale/late capture = 0x55.
$tgt = "C:\sar\ah3.dat"
VMArgs { param($t) $b=[byte[]]((,[byte]0xAA) * 4194304); [IO.File]::WriteAllBytes($t,$b) } @($tgt)
Start-Sleep -Milliseconds 500
$runOut = VMArgs { param($t) $o = & C:\sar\mmap_probe.exe $t lazy 3 2>&1; $o -join "`n" } @($tgt)
Write-Host "  run: $($runOut -replace "`n"," | ")"
Start-Sleep -Seconds 4
VMArgs { param($t) try { $fs=[IO.File]::Open($t,'Open','ReadWrite','ReadWrite'); $fs.Flush($true); $fs.Close() } catch {} } @($tgt)
Start-Sleep -Seconds 3

$ev = VM { & C:\sar\sarctl.exe events 256 2>&1 }
$evStr = ($ev | Out-String)
Set-Content -Path (Join-Path $Evid "AH3_events_$stamp.txt") -Value $evStr

$rows=@(); $probeRows=@(); $arm=@()
foreach ($line in $ev) {
    if ($line -match 'class=mmap-cap' -and $line -match 'sequence=(\d+).*actor=([0-9a-fA-F]{16})') {
        $val=[Convert]::ToUInt64($matches[2],16)
        $fb=[int](($val -shr 56) -band 0xFF)
        $ph=[int](($val -shr 40) -band 0xFFFF)
        $pg=[uint64](($val -shr 24) -band 0xFFFF)
        $st=[uint64]($val -band 0xFFFFFF)
        $rows += [pscustomobject]@{ seq=[uint64]$matches[1]; pathHash=('0x{0:X4}' -f $ph); pathHashRaw=$ph; firstByte=('0x{0:X2}' -f $fb); firstByteRaw=$fb; page=$pg; step=$st }
    }
    if ($line -match 'class=arm-probe' -and $line -match 'actor=([0-9a-fA-F]{16})') {
        $v=[Convert]::ToUInt64($matches[1],16)
        $reqU=[int]($v -band 1); $prevU=[int](($v -shr 1) -band 1)
        $armpid=[uint64](($v -shr 8) -band 0xFFFFFF); $prot=[int](($v -shr 32) -band 0xFF)
        $mode = if ($reqU -eq 1 -and $prevU -eq 1) { 'USER' } else { 'KERN' }
        $arm += [pscustomobject]@{ mode=$mode; reqU=$reqU; prevU=$prevU; pid=$armpid; prot=('0x{0:X}' -f $prot) }
    }
    if ($line -match 'class=mmap-probe' -and $line -match 'actor=([0-9a-fA-F]{16})') {
        $v=[Convert]::ToUInt64($matches[1],16); $probeRows += [int](($v -shr 56) -band 0xFF)
    }
}
$arm | Export-Csv -Path (Join-Path $Evid "NOISE_arm_$stamp.csv") -NoTypeInformation
$armUser = ($arm | Where-Object { $_.mode -eq 'USER' })
$armKern = ($arm | Where-Object { $_.mode -eq 'KERN' })
Write-Host ""
Write-Host "==== NOISE classification: section-create arms (before gate) ====" -ForegroundColor Cyan
Write-Host "  total rw section-create arms observed: $($arm.Count)"
Write-Host "  USER-initiated (genuine mmap):   $($armUser.Count)   pids: $((($armUser.pid | Sort-Object -Unique) -join ','))"
Write-Host "  KERN-initiated (Cc data section): $($armKern.Count)   pids: $((($armKern.pid | Sort-Object -Unique) -join ','))"
Write-Host "  -- with the UserMode gate active, only USER arms proceed to capture --"
$rows = $rows | Sort-Object seq
$rows | Export-Csv -Path (Join-Path $Evid "AH3_decoded_$stamp.csv") -NoTypeInformation

# ah3.dat = the path-hash whose captures are the 4096-step 0xAA pages (our target signature)
$targetHash = ($rows | Where-Object { $_.firstByteRaw -eq 0xAA -and $_.step -eq 4096 } |
                Group-Object pathHashRaw | Sort-Object Count -Descending | Select-Object -First 1).Name
$tgtRows = $rows | Where-Object { $_.pathHashRaw -eq [int]$targetHash }
$otherRows = $rows | Where-Object { $_.pathHashRaw -ne [int]$targetHash }

$irqlNon0 = ($probeRows | Where-Object { $_ -ne 0 }).Count

Write-Host ""
Write-Host "==== A-H3 result (eager OFF; paging-write region capture only) ====" -ForegroundColor Cyan
Write-Host "  total mmap-cap rows: $($rows.Count)   target pathHash (ah3.dat) = 0x$('{0:X4}' -f [int]$targetHash)"
Write-Host "  -- captures on ah3.dat (target file) --"
Write-Host "     rows: $($tgtRows.Count)"
Write-Host "     firstByte==0xAA (PRE-IMAGE):  $(($tgtRows | Where-Object { $_.firstByteRaw -eq 0xAA }).Count)"
Write-Host "     firstByte==0x55 (POST-IMAGE): $(($tgtRows | Where-Object { $_.firstByteRaw -eq 0x55 }).Count)"
Write-Host "     firstByte other:             $(($tgtRows | Where-Object { $_.firstByteRaw -ne 0xAA -and $_.firstByteRaw -ne 0x55 }).Count)"
Write-Host "     distinct pages: $(($tgtRows | Select-Object -ExpandProperty page | Sort-Object -Unique).Count)  (file=1024 pages, dirtied ~64 stride16)"
Write-Host "  -- captures on OTHER files (distinct pathHashes) --"
Write-Host "     rows: $($otherRows.Count)   distinct other files (pathHashes): $(($otherRows | Select-Object -ExpandProperty pathHashRaw -Unique).Count)"
Write-Host "  mmap-probe pre-ops with IRQL != PASSIVE: $irqlNon0"
Write-Host ""
$tgtPost = ($tgtRows | Where-Object { $_.firstByteRaw -eq 0x55 }).Count
$tgtOther = ($tgtRows | Where-Object { $_.firstByteRaw -ne 0xAA -and $_.firstByteRaw -ne 0x55 }).Count
$tgtPre  = ($tgtRows | Where-Object { $_.firstByteRaw -eq 0xAA }).Count
$otherFiles = ($otherRows | Select-Object -ExpandProperty pathHashRaw -Unique).Count
Write-Host ""
Write-Host "==== NOISE test verdict (UserMode arm gate applied) ====" -ForegroundColor Cyan
if ($armKern.Count -gt 0 -and $otherFiles -eq 0 -and $tgtPre -gt 0 -and $tgtPost -eq 0 -and $tgtOther -eq 0) {
    Write-Host "  PASS - Confirmed (b): the 'noise' streams arm as KERN (cache-manager data sections)." -ForegroundColor Green
    Write-Host "         The UserMode gate removed ALL of them: 0 other-file captures remain," -ForegroundColor Green
    Write-Host "         while the genuine USER-initiated target mapping still captured its $tgtPre pre-image regions." -ForegroundColor Green
} elseif ($otherFiles -gt 0) {
    Write-Host "  INVESTIGATE - $otherFiles other files still captured after the gate; check their arm mode (may be genuine USER mmap by a system service -> allow-list, not gate)." -ForegroundColor Yellow
} else {
    Write-Host "  INVESTIGATE - unexpected: armKern=$($armKern.Count) otherFiles=$otherFiles tgtPre=$tgtPre tgtPost=$tgtPost tgtOther=$tgtOther" -ForegroundColor Yellow
}
Write-Host "  Evidence: $Evid"
