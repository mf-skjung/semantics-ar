param(
    [string]$VMName = "SarTarget",
    [string]$Repo = (Join-Path $PSScriptRoot ".."),
    [PSCredential]$Credential,
    [switch]$SkipRestore,
    [switch]$SkipDeploy
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
    Write-Host "  PS Direct after restore: $ping"
}

if (-not $SkipDeploy) {
    Write-Host "Deploy: instrumented driver + service + sarctl + mmap_probe" -ForegroundColor Yellow
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
    $loaded = VM { [bool]((fltmc filters 2>$null) -match 'semantics_ar') }
    if (-not $loaded) { throw "minifilter failed to load: $loadOut" }
    VM {
        $p="C:\sar\semantics_ar_service.exe"
        if (-not (Get-Service semantics_ar_service -ErrorAction SilentlyContinue)) { New-Service -Name semantics_ar_service -BinaryPathName $p -StartupType Manual -ErrorAction SilentlyContinue | Out-Null }
        Start-Service semantics_ar_service -ErrorAction SilentlyContinue; Start-Sleep -Seconds 4
    }
}
$live = VM { [bool]((fltmc filters 2>$null) -match 'semantics_ar') }
Write-Host "  minifilter live: $live"
if (-not $live) { throw "driver not live" }

# generous budget so the mmap section arms (reserve succeeds); set a few times (B.2 latency)
foreach ($k in 1..3) { VM { & C:\sar\sarctl.exe budget 3600 512 2>&1 | Out-Null }; Start-Sleep -Milliseconds 800 }

$modes = @("lazy","explicit","pressure")
$allRaw = @()
foreach ($mode in $modes) {
    $tgt = "C:\sar\probe_$mode.dat"
    VMArgs { param($t) & fsutil file createnew $t 4194304 2>&1 | Out-Null } @($tgt)
    Start-Sleep -Milliseconds 500
    $runOut = VMArgs { param($t,$m) $o = & C:\sar\mmap_probe.exe $t $m 2 2>&1; $o -join "`n" } @($tgt,$mode)
    Write-Host "  run[$mode]: $($runOut -replace "`n"," | ")"
    Start-Sleep -Seconds 3
    # drain lazy write-back so any deferred paging writes surface
    VMArgs { param($t) try { $fs=[IO.File]::Open($t,'Open','ReadWrite','ReadWrite'); $fs.Flush($true); $fs.Close() } catch {} } @($tgt)
    Start-Sleep -Seconds 2
    $ev = VM { & C:\sar\sarctl.exe events 256 2>&1 }
    $evStr = ($ev | Out-String)
    Set-Content -Path (Join-Path $Evid "AH1_events_$mode`_$stamp.txt") -Value $evStr
    $allRaw += "=== MODE $mode ===`n$evStr`n"
}
Set-Content -Path (Join-Path $Evid "AH1_events_all_$stamp.txt") -Value ($allRaw -join "`n")

# decode packed actor field for class=mmap-probe rows across the final full dump
$final = VM { & C:\sar\sarctl.exe events 256 2>&1 }
$rows = @()
foreach ($line in $final) {
    if ($line -match 'class=mmap-probe' -and $line -match 'sequence=(\d+).*actor=([0-9a-fA-F]{16})') {
        $seq = [uint64]$matches[1]
        $val = [Convert]::ToUInt64($matches[2],16)
        $irql = [int](($val -shr 56) -band 0xFF)
        $topl = [int](($val -shr 55) -band 0x1)
        $sync = [int](($val -shr 54) -band 0x1)
        $want = [int](($val -shr 53) -band 0x1)
        $offp = [uint64](($val -shr 24) -band 0xFFFFFF)
        $len  = [uint64]($val -band 0xFFFFFF)
        $irqlName = switch ($irql) { 0 {"PASSIVE"} 1 {"APC"} 2 {"DISPATCH"} default {"IRQL$irql"} }
        $rows += [pscustomobject]@{ seq=$seq; irql=$irql; irqlName=$irqlName; toplevel=$topl; syncPaging=$sync; want=$want; offsetPage=$offp; length=$len }
    }
}
$rows = $rows | Sort-Object seq
$csv = Join-Path $Evid "AH1_decoded_$stamp.csv"
$rows | Export-Csv -Path $csv -NoTypeInformation
Write-Host ""
Write-Host "==== A-H1 IRQL distribution (mmap paging-write pre-op) ====" -ForegroundColor Cyan
$rows | Group-Object irqlName | ForEach-Object { Write-Host ("  {0,-10} {1}" -f $_.Name, $_.Count) }
Write-Host "==== A-H2 region-granularity sample (offsetPage,length) ====" -ForegroundColor Cyan
$rows | Select-Object -First 12 | ForEach-Object { Write-Host ("  seq=$($_.seq) irql=$($_.irqlName) topl=$($_.toplevel) sync=$($_.syncPaging) want=$($_.want) offPage=$($_.offsetPage) len=$($_.length)") }
$distinctPages = ($rows | Select-Object -ExpandProperty offsetPage | Sort-Object -Unique).Count
Write-Host "  distinct dirtied pages observed: $distinctPages   total probe rows: $($rows.Count)"
Write-Host ""
Write-Host "Evidence saved under: $Evid" -ForegroundColor Green
