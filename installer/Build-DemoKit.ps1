<#
.SYNOPSIS
    Regenerate the semantics-ar Demo Kit from current sources.

.DESCRIPTION
    Assembles a portable, self-verifying demo kit:
      - builds a SELF-CONTAINED payload (reusing Build-SarPackage.ps1 -SelfContainedApp),
      - copies the versioned kit scripts from demokit\ (the single source of truth),
      - generates the TEST MODE wallpaper,
      - stamps KIT-VERSION.json (kit version, product commit, dirty flag, payload hashes),
      - self-checks that the packaged installer matches the repo installer (no stale kit).

    The kit never forks the product installer; it packages the real one, so the kit stays
    current as the product evolves until production (WHQL) signing lands.

.PARAMETER Out
    Output kit directory (default: dist-demokit\SarDemoKit under the repo root).

.PARAMETER KitVersion
    Kit semver stamped into the banner and KIT-VERSION.json.

.PARAMETER SkipPayload
    Reuse an existing payload (skip dotnet publish / driver copy) for fast script iteration.

.PARAMETER Zip
    Also produce SarDemoKit-<ver>.zip next to the kit for handoff.
#>
[CmdletBinding()]
param(
    [string]$Repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path,
    [string]$Out,
    [string]$KitVersion = '0.9.0',
    [switch]$SkipPayload,
    [switch]$Zip
)
$ErrorActionPreference = 'Stop'

$src = Join-Path $Repo 'demokit'
if (-not (Test-Path $src)) { throw "kit source not found: $src" }
if (-not $Out) { $Out = Join-Path $Repo 'dist-demokit\SarDemoKit' }

Write-Host "building demo kit -> $Out" -ForegroundColor Cyan

# 1) kit scripts (versioned source of truth)
if (Test-Path $Out) { Remove-Item $Out -Recurse -Force }
New-Item -ItemType Directory -Force -Path $Out | Out-Null
Copy-Item (Join-Path $src '*') $Out -Recurse -Force

# 2) self-contained payload (reuses the product installer + package builder)
$payload = Join-Path $Out 'payload'
if (-not $SkipPayload) {
    & (Join-Path $PSScriptRoot 'Build-SarPackage.ps1') -Dest $payload -SelfContainedApp
    if ($LASTEXITCODE -ne 0 -and -not (Test-Path (Join-Path $payload 'SemanticsAr-Setup.ps1'))) {
        throw 'payload build failed'
    }
} elseif (-not (Test-Path (Join-Path $payload 'SemanticsAr-Setup.ps1'))) {
    throw "-SkipPayload set but no existing payload at $payload"
}

# 3) TEST MODE wallpaper
$assets = Join-Path $Out 'assets'
New-Item -ItemType Directory -Force -Path $assets | Out-Null
$wall = Join-Path $assets 'testmode-wallpaper.png'
Add-Type -AssemblyName System.Drawing
$bmp = New-Object System.Drawing.Bitmap(1920, 1080)
$g = [System.Drawing.Graphics]::FromImage($bmp)
try {
    $g.Clear([System.Drawing.Color]::FromArgb(28, 27, 26))
    $g.SmoothingMode = 'AntiAlias'
    $g.TextRenderingHint = 'AntiAliasGridFit'
    $red = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(220, 60, 60))
    $mut = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(150, 148, 145))
    $fBig = New-Object System.Drawing.Font('Segoe UI', 64, [System.Drawing.FontStyle]::Bold)
    $fMid = New-Object System.Drawing.Font('Segoe UI', 26, [System.Drawing.FontStyle]::Regular)
    $fmt = New-Object System.Drawing.StringFormat
    $fmt.Alignment = 'Center'; $fmt.LineAlignment = 'Center'
    $g.DrawString('semantics-ar  -  TEST MODE', $fBig, $red, (New-Object System.Drawing.RectangleF(0, 380, 1920, 140)), $fmt)
    $g.DrawString("Not production-signed  -  Demo VM only  -  v$KitVersion", $fMid, $mut, (New-Object System.Drawing.RectangleF(0, 540, 1920, 80)), $fmt)
    $bmp.Save($wall, [System.Drawing.Imaging.ImageFormat]::Png)
} finally {
    $g.Dispose(); $bmp.Dispose()
}

# 4) stamp KIT-VERSION.json
function Get-PayloadHashes {
    param([string]$PayloadRoot)
    $rels = @(
        'driver\semantics_ar.sys',
        'driver\semantics_ar_elam.sys',
        'app\SemanticsAr.App.exe'
    )
    $h = [ordered]@{}
    foreach ($rel in $rels) {
        $f = Join-Path $PayloadRoot $rel
        if (Test-Path $f) { $h["payload\$rel"] = 'sha256:' + (Get-FileHash $f -Algorithm SHA256).Hash }
    }
    return $h
}
Push-Location $Repo
$commit = (& git rev-parse --short HEAD 2>$null); if (-not $commit) { $commit = 'unknown' }
$dirty = [bool]((& git status --porcelain 2>$null) | Where-Object { $_ })
Pop-Location
$selfContained = Test-Path (Join-Path $payload 'app\.self-contained')

$stamp = [ordered]@{
    kitVersion       = $KitVersion
    productCommit    = $commit
    productDirty     = $dirty
    builtUtc         = (Get-Date).ToUniversalTime().ToString('o')
    selfContainedApp = $selfContained
    testMode         = $true
    payload          = (Get-PayloadHashes $payload)
}
($stamp | ConvertTo-Json -Depth 6) | Set-Content -Path (Join-Path $Out 'KIT-VERSION.json') -Encoding UTF8

# 5) self-check: packaged installer must equal the repo installer (kit is not stale/forked)
$kitSetup = Join-Path $payload 'SemanticsAr-Setup.ps1'
$repoSetup = Join-Path $PSScriptRoot 'SemanticsAr-Setup.ps1'
if ((Test-Path $kitSetup) -and (Test-Path $repoSetup)) {
    if ((Get-FileHash $kitSetup).Hash -ne (Get-FileHash $repoSetup).Hash) {
        throw 'kit installer differs from repo installer - the kit is stale; rebuild the payload'
    }
}
# every kit entry point must exist
foreach ($f in 'Start-SarDemo.ps1', 'Reset-SarDemo.ps1', 'Demo.cmd', 'attack\Start-Attack.ps1', 'attack\Seed-Sandbox.ps1', 'lib\Preflight.psm1') {
    if (-not (Test-Path (Join-Path $Out $f))) { throw "kit missing required file: $f" }
}

Write-Host ''
Write-Host ("demo kit ready: {0}" -f $Out) -ForegroundColor Green
Write-Host ("  version   : {0}+g{1}{2}" -f $KitVersion, $commit, $(if ($dirty) { ' (uncommitted)' } else { '' })) -ForegroundColor Gray
Write-Host ("  self-contained app: {0}" -f $selfContained) -ForegroundColor Gray

if ($Zip) {
    $zipPath = Join-Path (Split-Path $Out -Parent) ("SarDemoKit-$KitVersion.zip")
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    Compress-Archive -Path (Join-Path $Out '*') -DestinationPath $zipPath -Force
    Write-Host ("  zipped    : {0}" -f $zipPath) -ForegroundColor Gray
}
