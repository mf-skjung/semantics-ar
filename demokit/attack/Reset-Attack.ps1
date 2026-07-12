<#
.SYNOPSIS
    Re-seed the sandbox for a repeat demo (restores the original sample files and clears the
    ransom note). Independent of product recovery, so you can re-demo even right after an
    AUDIT-mode encryption.
#>
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'Seed-Sandbox.ps1')
Write-Host 'sandbox reset - ready for another demo run.' -ForegroundColor Green
