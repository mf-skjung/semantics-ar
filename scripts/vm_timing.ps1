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
$script:Sess = $null
function Connect-VM {
    if ($script:Sess -and $script:Sess.State -eq 'Opened') { return }
    if ($script:Sess) { Remove-PSSession $script:Sess -ErrorAction SilentlyContinue; $script:Sess = $null }
    for ($i = 0; $i -lt 10; $i++) {
        try { $script:Sess = New-PSSession -VMName $VMName -Credential $Credential -ErrorAction Stop; return }
        catch { Start-Sleep -Seconds 5 }
    }
    throw "no session"
}
function VM { param([scriptblock]$s) Connect-VM; Invoke-Command -Session $script:Sess -ScriptBlock $s }
function VMArgs { param([scriptblock]$s,[object[]]$a) Connect-VM; Invoke-Command -Session $script:Sess -ScriptBlock $s -ArgumentList $a }
function CopyToVM { param($l,$r) Connect-VM; Copy-Item -Path $l -Destination $r -ToSession $script:Sess -Force }
function T { param([string]$name,[scriptblock]$s)
    $t = Measure-Command { & $s }
    "{0,-42} {1,8:N1} s" -f $name, $t.TotalSeconds | Write-Host
}

$pkg = "$Repo\build_driver\pkg"
Write-Host "=== PER-STEP TIMING (no restore; VM already up) ===" -ForegroundColor Cyan

T "connect PS Direct session" { Connect-VM }
T "unload old filter + clear dir" {
    try { VM { Stop-Service semantics_ar_service -Force -ErrorAction SilentlyContinue } } catch {}
    try { VM { fltmc unload semantics_ar 2>$null } } catch {}
    VM { $s="C:\Windows\System32\drivers\SemanticsAr"; if (Test-Path $s){ Get-ChildItem $s -Recurse -EA SilentlyContinue | Remove-Item -Recurse -Force -EA SilentlyContinue } }
}
T "copy semantics_ar.sys (213 KB)"     { CopyToVM "$pkg\semantics_ar.sys" "C:\sar\semantics_ar.sys" }
T "copy semantics_ar.cat"              { CopyToVM "$pkg\semantics_ar.cat" "C:\sar\semantics_ar.cat" }
T "copy semantics_ar.inf"              { CopyToVM "$pkg\semantics_ar.inf" "C:\sar\semantics_ar.inf" }
T "copy SemanticsArTest.cer"           { CopyToVM "$pkg\SemanticsArTest.cer" "C:\sar\SemanticsArTest.cer" }
T "copy service exe (173 KB)"          { CopyToVM "$Repo\build_win\service\Release\semantics_ar_service.exe" "C:\sar\semantics_ar_service.exe" }
T "copy sarctl.exe"                    { CopyToVM "$Repo\build_win\tools\Release\sarctl.exe" "C:\sar\sarctl.exe" }
T "copy mmap_probe.exe"                { CopyToVM "$Repo\build_harness\mmap_probe.exe" "C:\sar\mmap_probe.exe" }
T "certutil + pnputil + service reg" {
    VM {
        $cer="C:\sar\SemanticsArTest.cer"
        certutil -addstore Root $cer 2>$null | Out-Null; certutil -addstore TrustedPublisher $cer 2>$null | Out-Null
        pnputil /add-driver C:\sar\semantics_ar.inf /install 2>$null | Out-Null
        $svc="HKLM:\SYSTEM\CurrentControlSet\Services\semantics_ar"
        New-Item -Path $svc -Force | Out-Null
        Set-ItemProperty $svc -Name ImagePath -Value "\??\C:\sar\semantics_ar.sys"
        Set-ItemProperty $svc -Name Type -Value 2 -Type DWord
        Set-ItemProperty $svc -Name Start -Value 3 -Type DWord
        Set-ItemProperty $svc -Name Group -Value "FSFilter Activity Monitor"
        New-Item -Path "$svc\Instances" -Force | Out-Null
        Set-ItemProperty "$svc\Instances" -Name DefaultInstance -Value "semantics_ar Instance"
        New-Item -Path "$svc\Instances\semantics_ar Instance" -Force | Out-Null
        Set-ItemProperty "$svc\Instances\semantics_ar Instance" -Name Altitude -Value "385000"
        Set-ItemProperty "$svc\Instances\semantics_ar Instance" -Name Flags -Value 0 -Type DWord
    }
}
T "fltmc load + settle 3s" { VM { fltmc load semantics_ar 2>&1 | Out-Null }; Start-Sleep -Seconds 3 }
T "create service + start + settle 4s" {
    VM { $p="C:\sar\semantics_ar_service.exe"; if (-not (Get-Service semantics_ar_service -EA SilentlyContinue)) { New-Service -Name semantics_ar_service -BinaryPathName $p -StartupType Manual -EA SilentlyContinue | Out-Null }; Start-Service semantics_ar_service -EA SilentlyContinue }
    Start-Sleep -Seconds 4
}
T "sarctl budget x3 (+0.8s sleeps)" { foreach ($k in 1..3) { VM { & C:\sar\sarctl.exe budget 3600 512 2>&1 | Out-Null }; Start-Sleep -Milliseconds 800 } }
T "create 4MB target (fsutil)" { VMArgs { param($t) & fsutil file createnew $t 4194304 2>&1 | Out-Null } @("C:\sar\tmg.dat") }
T "run mmap_probe (hold 2s)" { VMArgs { param($t) & C:\sar\mmap_probe.exe $t lazy 2 2>&1 | Out-Null } @("C:\sar\tmg.dat") }
T "post-run settle 3s" { Start-Sleep -Seconds 3 }
T "flush target (FileStream)" { VMArgs { param($t) try { $fs=[IO.File]::Open($t,'Open','ReadWrite','ReadWrite'); $fs.Flush($true); $fs.Close() } catch {} } @("C:\sar\tmg.dat") }
T "settle 2s" { Start-Sleep -Seconds 2 }
T "sarctl events 256 (read pipe)" { $script:ev = VM { & C:\sar\sarctl.exe events 256 2>&1 } }
Write-Host ("  (events read: {0} lines)" -f ($script:ev | Measure-Object).Count)
Write-Host "=== done ===" -ForegroundColor Cyan
Get-PSSession | Remove-PSSession -ErrorAction SilentlyContinue
