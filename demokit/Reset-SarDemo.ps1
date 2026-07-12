<#
.SYNOPSIS
    semantics-ar Demo Kit teardown - return a demo VM to a normal state.

.DESCRIPTION
    Reverses exactly what the kit changed: uninstalls the product (transactional), turns
    test signing off, restores Memory Integrity if the kit disabled it, removes the resume
    task / auto-logon / TEST MODE wallpaper, and deletes the sandbox. One reboot finalizes.
    Idempotent: anything already in its normal state is skipped.
#>
[CmdletBinding()]
param(
    [switch]$Resume, [switch]$Status, [switch]$Reset,
    [switch]$IAmSure, [switch]$AutoLogon, [switch]$Force, [switch]$Assume, [switch]$NoElevate
)

$ErrorActionPreference = 'Stop'
$KitRoot = $PSScriptRoot

function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}
if (-not (Test-Admin) -and -not $NoElevate) {
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-NoElevate')
    if ($Force)  { $argList += '-Force' }
    if ($Assume) { $argList += '-Assume' }
    try { Start-Process -FilePath 'powershell.exe' -ArgumentList $argList -Verb RunAs | Out-Null }
    catch { Write-Host 'Teardown needs Administrator rights. Approve UAC or re-run elevated.' -ForegroundColor Red }
    return
}

Import-Module (Join-Path $KitRoot 'lib\Ui.psm1') -Force
Import-Module (Join-Path $KitRoot 'lib\Preflight.psm1') -Force
Import-Module (Join-Path $KitRoot 'lib\State.psm1') -Force
Import-Module (Join-Path $KitRoot 'lib\BootPolicy.psm1') -Force
Import-Module (Join-Path $KitRoot 'lib\Guard.psm1') -Force

Write-SarBanner -KitVersion 'reset' -ProductCommit '' -Dirty $false
Write-Phase 'TEARDOWN'

$st = Get-SarState

# 1) product uninstall (delegate to the product's own transactional uninstall)
$setup = Join-Path $KitRoot 'payload\SemanticsAr-Setup.ps1'
if (Test-Path $setup) {
    try {
        $out = & powershell -NoProfile -ExecutionPolicy Bypass -File $setup -Action Uninstall *>&1 | Out-String
        Write-Step 'product uninstall (transactional)' 'OK'
        if ($out -match '(?i)reboot') { Write-SarLog '(PPL service may be reboot-pending - finalizes on reboot)' 'warn' }
    } catch {
        Write-Step 'product uninstall reported an issue' 'WARN' 'warn'
    }
} else {
    Write-Step 'no payload installer present - skipping product uninstall' 'SKIP'
}

# 2) resume task + auto-logon
Unregister-ResumeTask
Clear-AutoLogon
Write-Step 'resume task removed, auto-logon cleared' 'OK'

# 3) wallpaper
$priorWall = if ($st) { [string]$st.priorWallpaper } else { '' }
Restore-Wallpaper -ImagePath $priorWall
Write-Step 'wallpaper restored' 'OK'

# 4) Memory Integrity - only re-enable if the kit is the one that turned it off
$hvciChange = if ($st -and $st.bootChanges) { [string]$st.bootChanges.hvci } else { '' }
if ($hvciChange -eq 'disabled-was-on') {
    Restore-Hvci | Out-Null
    Write-Step 'Memory Integrity restored (was ON)' 'OK'
} else {
    Write-Step 'Memory Integrity left as-is (kit did not change it)' 'SKIP'
}

# 5) test signing off
$r = Disable-TestSigning
if ($r.Changed) { Write-Step 'testsigning -> OFF' 'done' } else { Write-Step 'test signing already off' 'SKIP' }

# 6) sandbox + state
$root = Get-SarStateRoot
if (Test-Path $root) {
    # Remove the sandbox but keep nothing behind; state file goes with it.
    Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue
    Write-Step ('sandbox + state removed ({0})' -f $root) 'OK'
}

Write-Host ''
Write-Host 'This VM will return to a normal state after ONE reboot.' -ForegroundColor Yellow
if (Confirm-YesNo 'Reboot now?' -DefaultYes $true -Assume:($Assume -or $Force)) {
    Restart-Computer -Force
}
