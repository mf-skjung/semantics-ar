<#
.SYNOPSIS
    Run the SAFE, sandboxed demo attack - an in-place high-entropy overwrite of the sample
    files, which is exactly the ransomware behaviour semantics-ar is built to catch.

.DESCRIPTION
    There is intentionally NO target parameter: the attack can only ever touch the fixed
    sandbox (C:\SarDemo\Sandbox), gated by a canary and a manifest. Use it to show:
      - ENFORCE: the product blocks the very first overwrite (files stay intact)
      - AUDIT:   the product records the event and keeps the files recoverable
    See RUNBOOK.md for the presenter narration.
#>
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'lib\SafeEncryptor.ps1')
$root = Get-SandboxRoot

Write-Host ''
Write-Host '============================================================' -ForegroundColor Red
Write-Host '  DEMO ATTACK (safe, sandboxed)  ->  ' -NoNewline -ForegroundColor Red
Write-Host $root -ForegroundColor Yellow
Write-Host '  Simulating in-place encryption of the sample documents...' -ForegroundColor Red
Write-Host '============================================================' -ForegroundColor Red

$result = Invoke-SafeEncrypt

Write-Host ''
Write-Host ("  Files targeted : {0}" -f $result.Attempted) -ForegroundColor Gray
Write-Host ("  Overwrites that reached disk : {0}" -f $result.Written) -ForegroundColor Gray
if ($result.Written -eq 0 -and $result.Attempted -gt 0) {
    Write-Host '  => EVERY overwrite was blocked. This is ENFORCE mode working.' -ForegroundColor Green
    Write-Host '     Open any sample file - it is still intact.' -ForegroundColor Green
} elseif ($result.Written -gt 0) {
    Write-Host '  => Files were encrypted (AUDIT mode records, does not block).' -ForegroundColor Yellow
    Write-Host '     Use the semantics-ar app to RECOVER them, or run Reset-Attack.ps1.' -ForegroundColor Yellow
}
Write-Host ''
