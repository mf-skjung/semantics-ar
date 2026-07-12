<#
.SYNOPSIS
    semantics-ar product installer / uninstaller / verifier.

.DESCRIPTION
    Installs the full semantics-ar stack from a staged payload directory produced by
    Build-SarPackage.ps1: the ELAM-signed minifilter driver, the protected (PPL-AM) user
    service, the COM elevation host, and the WPF application. Uninstall reverses every step
    and reports whether any component could only be removed on reboot (a PPL-AM service
    cannot be stopped live by a non-protected caller). Verify asserts the running product.

    The operation is transactional: on any install failure the steps taken so far are undone
    in reverse. Re-running Install is idempotent.

.PARAMETER Action
    Install | Uninstall | Verify.

.PARAMETER Source
    Payload directory (defaults to this script's directory). Must contain app\ and driver\.

.PARAMETER InstallRoot
    Target directory (defaults to "%ProgramFiles%\semantics-ar").

.PARAMETER NoProtect
    Skip ELAM registration + PPL-AM launch protection (service runs unprotected). Use for a
    development install where the reboot-to-uninstall friction of a protected service is not
    wanted. Production installs should leave protection on.
#>
[CmdletBinding()]
param(
    [ValidateSet('Install', 'Uninstall', 'Verify')]
    [string]$Action = 'Install',
    [string]$Source = $PSScriptRoot,
    [string]$InstallRoot = (Join-Path $env:ProgramFiles 'semantics-ar'),
    [switch]$NoProtect
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$UserService   = 'SemanticsAr'
$DriverService = 'semantics_ar'
$ElamService   = 'semantics_ar_elam'
$AppExe        = 'SemanticsAr.App.exe'
$HostExe       = 'SemanticsArElevationHost.exe'
$ServiceExe    = 'semantics_ar_service.exe'
$InstallerExe  = 'sar_install.exe'
$ComClsid      = '{B3F2A6C1-5D84-4E2A-9C77-1E5A0D9C4A12}'
$TestCertSubj  = 'CN=SemanticsAr Test'
$UninstallKey  = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\SemanticsAr'
$ShortcutPath  = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'semantics-ar.lnk'
$LogPath       = Join-Path $env:ProgramData ('semantics-ar\setup-{0}.log' -f $Action.ToLower())

# ── logging ───────────────────────────────────────────────────────────────────────────────
New-Item -ItemType Directory -Force -Path (Split-Path $LogPath) | Out-Null
function Log {
    param([string]$Message, [string]$Level = 'INFO')
    $line = '{0} [{1,-5}] {2}' -f (Get-Date -Format 'HH:mm:ss.fff'), $Level, $Message
    switch ($Level) {
        'FAIL' { Write-Host $line -ForegroundColor Red }
        'WARN' { Write-Host $line -ForegroundColor Yellow }
        'OK'   { Write-Host $line -ForegroundColor Green }
        default { Write-Host $line }
    }
    Add-Content -Path $LogPath -Value $line
}
function Fail { param([string]$m) Log $m 'FAIL'; throw $m }

# ── elevation + platform preflight ──────────────────────────────────────────────────────────
function Assert-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $pr = New-Object Security.Principal.WindowsPrincipal($id)
    if (-not $pr.IsInRole([Security.Principal.WindowsBuiltinRole]::Administrator)) {
        Fail 'Administrator privileges are required. Re-run from an elevated prompt.'
    }
}
function Test-DesktopRuntime {
    $rt = & dotnet --list-runtimes 2>$null
    return [bool]($rt | Select-String -SimpleMatch 'Microsoft.WindowsDesktop.App 10.')
}

# ── native command wrapper (captures + logs, no PS-native-stderr wrapping) ────────────────────
function Invoke-Native {
    param([Parameter(Mandatory)][string]$File, [string[]]$Arguments, [int[]]$Ok = @(0))
    $out = & $File @Arguments 2>&1 | Out-String
    $code = $LASTEXITCODE
    foreach ($l in ($out -split "`r?`n" | Where-Object { $_ })) { Log ("    | $l") }
    if ($Ok -notcontains $code) { Fail ("{0} {1} -> exit {2}" -f $File, ($Arguments -join ' '), $code) }
    return $out
}

# ── component probes (shared by Install idempotency, Uninstall, Verify) ───────────────────────
function Get-Svc { param([string]$Name) Get-Service -Name $Name -ErrorAction SilentlyContinue }
function Test-Filter { [bool]((& fltmc filters 2>$null) -match $DriverService) }
function Test-Com {
    [bool](Get-ItemProperty ("HKLM:\SOFTWARE\Classes\CLSID\$ComClsid\LocalServer32") -ErrorAction SilentlyContinue)
}
function Get-PublishedInf {
    # the pnputil OriginalName -> published oemNN.inf mapping, for a clean /delete-driver
    $enum = & pnputil /enum-drivers 2>$null | Out-String
    $blocks = $enum -split "(?m)^Published Name:\s*"
    foreach ($b in $blocks) {
        if ($b -match 'Original Name:\s*semantics_ar\.inf') {
            if ($b -match '^\s*(oem\d+\.inf)') { return $Matches[1] }
        }
    }
    return $null
}

# ════════════════════════════════════════════════════════════════════════════════════════════
# INSTALL
# ════════════════════════════════════════════════════════════════════════════════════════════
function Invoke-Install {
    Assert-Admin
    $appSrc = Join-Path $Source 'app'
    $drvSrc = Join-Path $Source 'driver'
    if (-not (Test-Path (Join-Path $appSrc $AppExe)))    { Fail "payload app\$AppExe not found under $Source" }
    if (-not (Test-Path (Join-Path $drvSrc "$DriverService.inf"))) { Fail "payload driver\$DriverService.inf not found under $Source" }
    if (-not (Test-DesktopRuntime)) {
        Fail '.NET 10 Desktop Runtime (Microsoft.WindowsDesktop.App 10.x) is required and was not found.'
    }
    Log "installing to $InstallRoot"

    $rollback = New-Object System.Collections.Generic.Stack[scriptblock]
    try {
        # 1 ─ payload
        New-Item -ItemType Directory -Force -Path $InstallRoot | Out-Null
        Copy-Item (Join-Path $appSrc '*') $InstallRoot -Recurse -Force
        Copy-Item $drvSrc (Join-Path $InstallRoot 'driver') -Recurse -Force
        $rollback.Push({ Remove-Item $InstallRoot -Recurse -Force -ErrorAction SilentlyContinue })
        Log 'payload copied' 'OK'

        $drvDir  = Join-Path $InstallRoot 'driver'
        $infPath = Join-Path $drvDir "$DriverService.inf"
        $cerPath = Join-Path $drvDir 'SemanticsArTest.cer'

        # 2 ─ trust the test-signing cert so a test-signed driver/service will load.
        #     A production (WHQL/attestation) driver is trusted by the OS already; detect and skip.
        $sig = Get-AuthenticodeSignature (Join-Path $drvDir "$DriverService.sys")
        $testSigned = ($sig.SignerCertificate -and $sig.SignerCertificate.Subject -eq $TestCertSubj)
        if ($testSigned -and (Test-Path $cerPath)) {
            foreach ($store in 'Root', 'TrustedPublisher') {
                Invoke-Native 'certutil.exe' @('-f', '-addstore', $store, $cerPath) | Out-Null
            }
            $rollback.Push({
                Get-ChildItem Cert:\LocalMachine\Root, Cert:\LocalMachine\TrustedPublisher -ErrorAction SilentlyContinue |
                    Where-Object Subject -eq $TestCertSubj | Remove-Item -Force -ErrorAction SilentlyContinue
            })
            Log 'test-signing certificate trusted (Root + TrustedPublisher)' 'OK'
        }

        # 3 ─ minifilter driver: pnputil stages to the driver store + registers the service.
        if (-not (Get-Svc $DriverService)) {
            Invoke-Native 'pnputil.exe' @('/add-driver', $infPath, '/install') @(0, 259) | Out-Null
            $rollback.Push({
                & fltmc unload $DriverService 2>$null | Out-Null
                $oem = Get-PublishedInf
                if ($oem) { & pnputil /delete-driver $oem /uninstall /force 2>$null | Out-Null }
                & sc.exe delete $DriverService 2>$null | Out-Null
            })
            Log 'minifilter driver registered' 'OK'
        }
        if (-not (Test-Filter)) {
            Invoke-Native 'fltmc.exe' @('load', $DriverService) | Out-Null
        }
        if (-not (Test-Filter)) { Fail 'minifilter did not attach after load' }
        Log 'minifilter loaded' 'OK'

        # 4 ─ user service (own-process; name must be SemanticsAr to match the dispatch table + PPL)
        if (-not (Get-Svc $UserService)) {
            $bin = '"{0}"' -f (Join-Path $InstallRoot $ServiceExe)
            Invoke-Native 'sc.exe' @('create', $UserService, 'binPath=', $bin, 'type=', 'own', 'start=', 'auto', 'DisplayName=', 'semantics-ar') | Out-Null
            $rollback.Push({ & sc.exe stop $UserService 2>$null | Out-Null; & sc.exe delete $UserService 2>$null | Out-Null })
            Log 'user service created' 'OK'
        }

        # 5 ─ ELAM certificate + PPL-AM launch protection (before first start so it launches protected)
        if (-not $NoProtect) {
            $elam = Join-Path $drvDir "$ElamService.sys"
            if (Test-Path $elam) {
                $rc = & (Join-Path $InstallRoot $InstallerExe) $elam 2>&1 | Out-String
                foreach ($l in ($rc -split "`r?`n" | Where-Object { $_ })) { Log "    | $l" }
                if ($LASTEXITCODE -ne 0) {
                    Log 'ELAM/PPL protection could not be applied; the service will run UNPROTECTED' 'WARN'
                } else {
                    $rollback.Push({ & sc.exe delete $ElamService 2>$null | Out-Null })
                    Log 'ELAM registered + PPL-AM launch protection set' 'OK'
                }
            } else { Log "ELAM image $ElamService.sys absent; skipping protection" 'WARN' }
        }

        # 6 ─ start the service
        Start-Service $UserService
        (Get-Svc $UserService).WaitForStatus('Running', '00:00:15')
        Log 'user service running' 'OK'

        # 7 ─ COM elevation host
        Invoke-Native (Join-Path $InstallRoot $HostExe) @('/RegServer') | Out-Null
        if (-not (Test-Com)) { Fail 'COM elevation host did not register (LocalServer32 missing)' }
        $rollback.Push({ & (Join-Path $InstallRoot $HostExe) /UnRegServer 2>$null | Out-Null })
        Log 'COM elevation host registered' 'OK'

        # 8 ─ Start Menu shortcut
        $ws = New-Object -ComObject WScript.Shell
        $sc = $ws.CreateShortcut($ShortcutPath)
        $sc.TargetPath = Join-Path $InstallRoot $AppExe
        $sc.WorkingDirectory = $InstallRoot
        $sc.Description = 'semantics-ar'
        $sc.Save()
        $rollback.Push({ Remove-Item $ShortcutPath -Force -ErrorAction SilentlyContinue })

        # 9 ─ Apps & Features registration
        New-Item -Path $UninstallKey -Force | Out-Null
        $selfPath = Join-Path $InstallRoot 'SemanticsAr-Setup.ps1'
        Copy-Item $PSCommandPath $selfPath -Force
        Set-ItemProperty $UninstallKey DisplayName    'semantics-ar'
        Set-ItemProperty $UninstallKey DisplayVersion '1.0.0.0'
        Set-ItemProperty $UninstallKey Publisher      'semantics-ar'
        Set-ItemProperty $UninstallKey InstallLocation $InstallRoot
        Set-ItemProperty $UninstallKey UninstallString ('powershell.exe -NoProfile -ExecutionPolicy Bypass -File "{0}" -Action Uninstall' -f $selfPath)
        Set-ItemProperty $UninstallKey NoModify 1 -Type DWord
        Set-ItemProperty $UninstallKey NoRepair 1 -Type DWord

        Log "install complete. Launch from the Start Menu (semantics-ar) or $InstallRoot\$AppExe" 'OK'
    }
    catch {
        Log "install failed: $($_.Exception.Message) — rolling back" 'FAIL'
        while ($rollback.Count -gt 0) { try { & $rollback.Pop() } catch { Log "  rollback step: $($_.Exception.Message)" 'WARN' } }
        throw
    }
}

# ════════════════════════════════════════════════════════════════════════════════════════════
# UNINSTALL
# ════════════════════════════════════════════════════════════════════════════════════════════
function Invoke-Uninstall {
    Assert-Admin
    $rebootRequired = $false
    Log "uninstalling from $InstallRoot"

    Remove-Item $ShortcutPath -Force -ErrorAction SilentlyContinue

    $hostPath = Join-Path $InstallRoot $HostExe
    if (Test-Path $hostPath) { & $hostPath /UnRegServer 2>&1 | Out-Null }
    if (-not (Test-Com)) { Log 'COM elevation host unregistered' 'OK' } else { Log 'COM CLSID still present' 'WARN' }

    # user service — a PPL-AM service cannot be stopped live by a non-protected caller; delete marks
    # it for removal and it is gone on the next reboot.
    if (Get-Svc $UserService) {
        & sc.exe stop $UserService 2>&1 | Out-Null
        Start-Sleep -Seconds 2
        $svc = Get-Svc $UserService
        if ($svc -and $svc.Status -ne 'Stopped') {
            Log "service $UserService is PPL-protected and cannot be stopped live; marking for delete (reboot required)" 'WARN'
            $rebootRequired = $true
        }
        & sc.exe delete $UserService 2>&1 | Out-Null
        Log 'user service deleted (or marked for delete)' 'OK'
    }

    # minifilter
    if (Test-Filter) { & fltmc unload $DriverService 2>&1 | Out-Null }
    $oem = Get-PublishedInf
    if ($oem) { & pnputil /delete-driver $oem /uninstall /force 2>&1 | Out-Null; Log "driver package $oem removed" 'OK' }
    if (Get-Svc $DriverService) { & sc.exe delete $DriverService 2>&1 | Out-Null }

    # ELAM service (the registered ELAM cert info clears with the service + reboot)
    if (Get-Svc $ElamService) { & sc.exe delete $ElamService 2>&1 | Out-Null; Log 'ELAM service deleted' 'OK' }

    # test-signing certificate (only ours, matched by subject)
    Get-ChildItem Cert:\LocalMachine\Root, Cert:\LocalMachine\TrustedPublisher -ErrorAction SilentlyContinue |
        Where-Object Subject -eq $TestCertSubj | ForEach-Object { Remove-Item $_.PSPath -Force -ErrorAction SilentlyContinue }

    # files + registration
    if (Test-Path $InstallRoot) {
        try { Remove-Item $InstallRoot -Recurse -Force }
        catch { Log "some files remain in use (service not yet stopped); they clear on reboot" 'WARN'; $rebootRequired = $true }
    }
    Remove-Item $UninstallKey -Recurse -Force -ErrorAction SilentlyContinue

    # orphan report
    $orphans = @()
    if (Get-Svc $UserService)   { $orphans += "service:$UserService(pending)" }
    if (Get-Svc $DriverService) { $orphans += "service:$DriverService" }
    if (Get-Svc $ElamService)   { $orphans += "service:$ElamService" }
    if (Test-Filter)            { $orphans += 'filter:loaded' }
    if (Test-Com)               { $orphans += 'com:registered' }
    if (Test-Path $InstallRoot) { $orphans += "files:$InstallRoot" }

    if ($orphans.Count -eq 0) { Log 'uninstall complete; no orphaned state' 'OK' }
    elseif ($rebootRequired)  { Log ("uninstall staged; reboot to finalize: {0}" -f ($orphans -join ', ')) 'WARN' }
    else                      { Log ("uninstall left orphans: {0}" -f ($orphans -join ', ')) 'FAIL' }

    if ($rebootRequired) { Log 'REBOOT REQUIRED to finalize removal of the protected service.' 'WARN' }
}

# ════════════════════════════════════════════════════════════════════════════════════════════
# VERIFY
# ════════════════════════════════════════════════════════════════════════════════════════════
function Invoke-Verify {
    $pass = 0; $fail = 0
    function Check { param([string]$n, [bool]$c) if ($c) { Log "PASS $n" 'OK'; $script:pass++ } else { Log "FAIL $n" 'FAIL'; $script:fail++ } }
    Check 'minifilter loaded'          (Test-Filter)
    Check 'driver service present'     ([bool](Get-Svc $DriverService))
    Check 'user service running'       ((Get-Svc $UserService).Status -eq 'Running')
    Check 'COM elevation registered'   (Test-Com)
    Check 'app present'                (Test-Path (Join-Path $InstallRoot $AppExe))
    Check 'elevation host present'     (Test-Path (Join-Path $InstallRoot $HostExe))
    Log ("verify: {0} passed, {1} failed" -f $pass, $fail) ($(if ($fail) { 'FAIL' } else { 'OK' }))
    if ($fail) { exit 1 }
}

switch ($Action) {
    'Install'   { Invoke-Install }
    'Uninstall' { Invoke-Uninstall }
    'Verify'    { Invoke-Verify }
}
