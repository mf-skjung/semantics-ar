<#
.SYNOPSIS
    semantics-ar Demo Kit - one-command, self-resuming setup for a throwaway Windows VM.

.DESCRIPTION
    Turns a fresh (or partially-configured) Windows VM into a demo-ready host for the
    test-signed semantics-ar product:  preflight GO/NO-GO -> apply boot policy (once) ->
    reboot -> auto-resume -> install product -> seed attack sandbox -> DEMO READY.

    Idempotent throughout: every step detects current reality and skips what is already
    done. A checkpoint that already has test signing on + Memory Integrity off needs NO
    reboot and installs immediately; a blank VM runs the full single-reboot flow.

.NOTES
    TEST MODE only. Not production-signed. Run on a disposable demo VM.
#>
[CmdletBinding()]
param(
    [switch]$Resume,       # used by the post-reboot scheduled task
    [switch]$Status,       # print state + preflight and exit (read-only)
    [switch]$Reset,        # delegate to Reset-SarDemo.ps1
    [switch]$IAmSure,      # override the throwaway-machine guard (non-VM / domain-joined)
    [switch]$AutoLogon,    # opt-in one-shot hands-free logon across the reboot
    [switch]$Force,        # skip confirmations (repair / unattended)
    [switch]$Assume,       # answer prompts with their default (non-interactive)
    [switch]$NoReboot,     # apply boot policy + schedule resume, but leave the reboot to the operator
    [switch]$NoElevate     # internal: do not attempt self-elevation (avoids relaunch loop)
)

$ErrorActionPreference = 'Stop'
$KitRoot = $PSScriptRoot

# ---- self-elevation: a single UAC prompt instead of a "not elevated" failure ----
function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}
if (-not (Test-Admin) -and -not $NoElevate) {
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-NoElevate')
    if ($Resume)    { $argList += '-Resume' }
    if ($Status)    { $argList += '-Status' }
    if ($Reset)     { $argList += '-Reset' }
    if ($IAmSure)   { $argList += '-IAmSure' }
    if ($AutoLogon) { $argList += '-AutoLogon' }
    if ($Force)     { $argList += '-Force' }
    if ($Assume)    { $argList += '-Assume' }
    try {
        Start-Process -FilePath 'powershell.exe' -ArgumentList $argList -Verb RunAs | Out-Null
    } catch {
        Write-Host 'This kit needs Administrator rights. Please approve the UAC prompt, or re-run elevated.' -ForegroundColor Red
    }
    return
}

Import-Module (Join-Path $KitRoot 'lib\Ui.psm1') -Force
Import-Module (Join-Path $KitRoot 'lib\Preflight.psm1') -Force
Import-Module (Join-Path $KitRoot 'lib\State.psm1') -Force
Import-Module (Join-Path $KitRoot 'lib\BootPolicy.psm1') -Force
Import-Module (Join-Path $KitRoot 'lib\Guard.psm1') -Force

# ---- kit identity / banner ----
$ver = $null
$verPath = Join-Path $KitRoot 'KIT-VERSION.json'
if (Test-Path $verPath) { $ver = Get-Content $verPath -Raw | ConvertFrom-Json }
$kitVer = if ($ver) { $ver.kitVersion } else { 'dev' }
$commit = if ($ver) { $ver.productCommit } else { 'unknown' }
$dirty = if ($ver) { [bool]$ver.productDirty } else { $false }

$mach = Get-MachineIdentity
$hostLine = 'Host : {0}   Hypervisor: {1}' -f $env:COMPUTERNAME, $(if ($mach.Hypervisor) { $mach.Hypervisor } else { 'not detected' })
Write-SarBanner -KitVersion $kitVer -ProductCommit $commit -Dirty $dirty -HostLine $hostLine

# ---- subcommands ----
if ($Reset) {
    & (Join-Path $KitRoot 'Reset-SarDemo.ps1') @PSBoundParameters
    return
}
if ($Status) {
    Write-Phase 'STATUS'
    $st = Get-SarState
    if ($st) { Write-SarLog ("state: {0}   (updated {1})" -f $st.state, $st.ts) } else { Write-SarLog 'state: (none - not started)' }
    Write-Phase 'PREFLIGHT (read-only)'
    foreach ($c in (Invoke-Preflight -KitRoot $KitRoot -IAmSure:$IAmSure)) { Write-Check -Status $c.Status -Name $c.Name -Detail $c.Detail }
    return
}

# ---- shared install/configure tail (used by both the no-reboot and resume paths) ----
$InstalledApp = Join-Path $env:ProgramFiles 'semantics-ar\SemanticsAr.App.exe'
$SandboxPath = Join-Path (Get-SarStateRoot) 'Sandbox'

function Invoke-ProductInstall {
    Write-Phase 'INSTALLING PRODUCT'
    $setup = Join-Path $KitRoot 'payload\SemanticsAr-Setup.ps1'
    if (-not (Test-Path $setup)) { throw "payload installer missing: $setup" }

    # Idempotent: if Verify already passes, the product is installed - skip re-install.
    $verify = & powershell -NoProfile -ExecutionPolicy Bypass -File $setup -Action Verify *>&1 | Out-String
    if ($verify -match '(?im)^\s*verify:\s*\d+\s+passed,\s*0\s+failed') {
        Write-Step 'product already installed (verify passed)' 'SKIP'
    } else {
        $install = & powershell -NoProfile -ExecutionPolicy Bypass -File $setup -Action Install -NoProtect *>&1 | Out-String
        Write-Host $install -ForegroundColor DarkGray
        $verify = & powershell -NoProfile -ExecutionPolicy Bypass -File $setup -Action Verify *>&1 | Out-String
    }

    if ($verify -notmatch '(?im)verify:\s*\d+\s+passed,\s*0\s+failed') {
        throw "product verify did not pass after install:`n$verify"
    }
    Write-Step 'product installed + verified' 'OK'

    # Independent confirmation the minifilter is actually attached.
    $flt = & fltmc filters 2>$null | Out-String
    if ($flt -notmatch 'semantics_ar') { throw 'minifilter did not attach (fltmc shows no semantics_ar filter)' }
    Write-Step 'minifilter attached (fltmc)' 'OK'
}

function Invoke-DemoConfigure {
    Write-Phase 'CONFIGURING DEMO'

    $seed = Join-Path $KitRoot 'attack\Seed-Sandbox.ps1'
    if (Test-Path $seed) {
        & $seed | Out-Null
        Write-Step ('attack sandbox seeded  {0}' -f $SandboxPath) 'OK'
    }

    $wall = Join-Path $KitRoot 'assets\testmode-wallpaper.png'
    if (Test-Path $wall) {
        $prior = Set-TestModeWallpaper -ImagePath $wall
        Save-SarState @{ priorWallpaper = $prior }
        Write-Step 'TEST MODE wallpaper applied' 'OK'
    }

    if (Test-Path $InstalledApp) {
        Start-Process -FilePath $InstalledApp -ErrorAction SilentlyContinue | Out-Null
        Write-Step 'semantics-ar app launched' 'OK'
    } else {
        Write-Step 'app exe not found at install path' 'WARN' 'warn'
    }
}

function Complete-Ready {
    Unregister-ResumeTask
    $st = Get-SarState
    if ($st -and $st.autoLogon) { Clear-AutoLogon }
    Save-SarState @{ state = 'READY' }
    Write-ReadyScreen -Mode 'AUDIT (product default) - switch to ENFORCE in-app to demo blocking' -SandboxPath $SandboxPath
}

function Invoke-InstallTail {
    try {
        Invoke-ProductInstall
        Invoke-DemoConfigure
        Complete-Ready
    } catch {
        Save-SarState @{ state = 'FAILED'; reason = "$_" }
        Write-Phase 'FAILED'
        Write-SarLog ("$_") 'err'
        Write-SarLog 'Likely cause: test signing not active (Secure Boot re-enabled?) or HVCI still on.' 'warn'
        if (Confirm-YesNo 'Roll back the partial product install now? (boot policy kept for retry)' -DefaultYes $true -Assume:$Assume) {
            $setup = Join-Path $KitRoot 'payload\SemanticsAr-Setup.ps1'
            & powershell -NoProfile -ExecutionPolicy Bypass -File $setup -Action Uninstall *>&1 | Out-String | Write-Host -ForegroundColor DarkGray
        }
        return
    }
}

# ---- RESUME path (post-reboot, already elevated via the scheduled task) ----
if ($Resume) {
    Write-Phase 'RESUMING (post-reboot)'
    if (Test-TestSigningOn) {
        Write-Check -Status 'GO' -Name 'Test signing now ACTIVE'
    } else {
        # Boot policy did not take. The usual cause is Secure Boot silently voiding it.
        if (Test-SecureBootOn) {
            Write-Check -Status 'STOP' -Name 'Secure Boot is ON - test signing cannot activate' -Detail 'disable Secure Boot in VM firmware/settings, then re-run Demo.cmd'
            Save-SarState @{ state = 'FAILED'; reason = 'secureboot-on-after-reboot' }
            Unregister-ResumeTask
            return
        }
        Write-Check -Status 'FIX' -Name 'Test signing not active yet - re-applying' -Detail 'a further reboot may be required'
        Enable-TestSigning | Out-Null
        Save-SarState @{ state = 'BOOT_PENDING' }
        if (Confirm-YesNo 'Reboot again to activate test signing?' -DefaultYes $true -Assume:$Assume) { Restart-Computer -Force }
        return
    }
    if (-not (Test-HvciOn)) { Write-Check -Status 'GO' -Name 'Memory Integrity now OFF' }
    Invoke-InstallTail
    return
}

# ---- FRESH run ----
Write-Phase 'PREFLIGHT'
$checks = Invoke-Preflight -KitRoot $KitRoot -IAmSure:$IAmSure
foreach ($c in $checks) { Write-Check -Status $c.Status -Name $c.Name -Detail $c.Detail }
$stops = @($checks | Where-Object { $_.Status -eq 'STOP' })
Write-Host ('-' * 64) -ForegroundColor DarkGray
if ($stops.Count -gt 0) {
    Write-Host 'VERDICT: NO-GO. Nothing was changed. Fix the [STOP] item(s) above and re-run Demo.cmd.' -ForegroundColor Red
    Save-SarState @{ state = 'NO_GO'; reason = ($stops.Key -join ',') }
    return
}

# Decide whether any boot change (hence a reboot) is actually needed.
$needTs = -not (Test-TestSigningOn)
$needHvci = Test-HvciOn

if (-not $needTs -and -not $needHvci) {
    Write-Host 'VERDICT: GO - boot policy already correct (test signing on, Memory Integrity off). No reboot needed.' -ForegroundColor Green
    Save-SarState @{ state = 'RESUMED'; kitPath = $KitRoot; bootChanges = @{} }
    Invoke-InstallTail
    return
}

$n = @($needTs, $needHvci | Where-Object { $_ }).Count
Write-Host ("VERDICT: GO - {0} boot change(s) needed, ONE reboot." -f $n) -ForegroundColor Green
if (-not (Confirm-YesNo 'Proceed?' -DefaultYes $true -Assume:($Assume -or $Force))) { return }

Write-Phase 'APPLYING BOOT POLICY'
$boot = @{}
if ($needTs) { $r = Enable-TestSigning; $boot['testsigning'] = 'enabled'; Write-Step 'bcdedit testsigning on' 'done' }
if ($needHvci) { $r = Disable-Hvci; $boot['hvci'] = 'disabled-was-on'; Write-Step 'Memory Integrity disabled (HVCI)' 'done' }
Register-ResumeTask -KitPath $KitRoot
Write-Step "auto-resume scheduled (task 'SarDemoResume')" 'done'

if ($AutoLogon) {
    $cred = Get-Credential -Message 'One-shot hands-free logon after reboot (throwaway VM only). Enter this VM account + password.'
    if ($cred) {
        $ok = Set-OneShotAutoLogon -UserName $cred.UserName -Password $cred.GetNetworkCredential().Password
        if ($ok) { Write-Step 'one-shot auto-logon armed (cleared at READY)' 'done' }
    }
}

Save-SarState @{ state = 'BOOT_PENDING'; kitPath = $KitRoot; bootChanges = $boot; autoLogon = [bool]$AutoLogon }

Write-Host ''
Write-Host 'The machine must reboot ONCE to activate test-signing mode.' -ForegroundColor Yellow
Write-Host 'This is unavoidable - test signing is a boot-loader policy.' -ForegroundColor Yellow
Write-Host '  * After reboot, LOG BACK IN with the same account.' -ForegroundColor Gray
Write-Host '  * The demo will CONTINUE AUTOMATICALLY (install + launch).' -ForegroundColor Gray
Write-Host ''
if ($NoReboot) {
    Write-Host 'Boot policy applied and auto-resume scheduled. Reboot when ready (-NoReboot set);' -ForegroundColor Gray
    Write-Host 'the demo resumes automatically at the next logon.' -ForegroundColor Gray
} elseif (Confirm-YesNo 'Reboot now?' -DefaultYes $true -Assume:($Assume -or $Force)) {
    Restart-Computer -Force
} else {
    Write-Host 'Reboot yourself when ready; the demo resumes automatically at the next logon.' -ForegroundColor Gray
}
