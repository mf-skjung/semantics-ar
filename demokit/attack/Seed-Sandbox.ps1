<#
.SYNOPSIS
    Create (or reset) the walled-off demo sandbox with realistic-looking sample documents.

.DESCRIPTION
    Populates C:\SarDemo\Sandbox with deterministic sample files (so re-seeding restores
    identical originals), writes the canary the attack primitive requires, and records the
    manifest that bounds what the attack may touch. Safe: it only ever writes inside the
    fixed sandbox root.
#>
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'lib\SafeEncryptor.ps1')
$root = Get-SandboxRoot

New-Item -ItemType Directory -Force -Path $root | Out-Null

# Canary: the attack primitive refuses to run without this exact marker.
$kitVer = 'dev'
$verPath = Join-Path $PSScriptRoot '..\KIT-VERSION.json'
if (Test-Path $verPath) { try { $kitVer = (Get-Content $verPath -Raw | ConvertFrom-Json).kitVersion } catch { } }
Set-Content -Path (Join-Path $root '.sar-sandbox') -Encoding ASCII -Value @"
SAR-DEMO-SANDBOX
kit=$kitVer
note=Safe throwaway demo files. Attack scripts operate ONLY inside this folder.
"@

# Deterministic sample documents. Same name -> same content, so Reset-Attack regenerates
# the exact originals (independent of product recovery).
$lorem = 'Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. '
$docs = [ordered]@{
    'quarterly-report.txt'     = 40
    'budget-2026.csv'          = 30
    'meeting-notes.md'         = 20
    'contract-draft.txt'       = 60
    'customer-list.csv'        = 35
    'product-roadmap.md'       = 25
    'invoice-1042.txt'         = 12
    'presentation-outline.txt' = 18
    'research-summary.md'      = 45
    'photo-index.txt'          = 15
    'account-backup.txt'       = 10
    'readme.txt'               = 8
}

$files = @()
foreach ($name in $docs.Keys) {
    $reps = [int]$docs[$name]
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine("# $name")
    [void]$sb.AppendLine("Demo document - safe sandbox content.")
    [void]$sb.AppendLine('')
    for ($i = 0; $i -lt $reps; $i++) { [void]$sb.AppendLine(("{0:D3}  {1}" -f $i, $lorem)) }
    Set-Content -Path (Join-Path $root $name) -Value $sb.ToString() -Encoding UTF8
    $files += $name
}

# Manifest: the attack primitive will touch ONLY these paths.
@{ files = $files; seededUtc = (Get-Date).ToUniversalTime().ToString('o') } |
    ConvertTo-Json | Set-Content -Path (Join-Path $root 'sandbox-manifest.json') -Encoding UTF8

# Remove any leftover ransom note from a prior run.
Remove-Item (Join-Path $root 'READ_ME_TO_RESTORE.txt') -ErrorAction SilentlyContinue

Write-Host ("seeded {0} sample files in {1}" -f $files.Count, $root) -ForegroundColor Green
