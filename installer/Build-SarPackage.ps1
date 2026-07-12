<#
.SYNOPSIS
    Assemble a distributable semantics-ar install payload from the build outputs.

.DESCRIPTION
    Produces a self-contained payload directory that SemanticsAr-Setup.ps1 consumes:

        <Dest>\
            SemanticsAr-Setup.ps1
            app\      framework-dependent WPF publish + native surface (sarapi, COM host, service, sar_install)
            driver\   signed minifilter + ELAM + catalog + test-signing certificate

    The driver payload is taken from the signed package produced by scripts\package_driver.ps1
    (build_driver\pkg). The native surface is taken from the Release CMake tree (build_win). The
    app is a fresh framework-dependent `dotnet publish` (the VM carries the .NET 10 Desktop Runtime).

.PARAMETER Dest
    Output payload directory (default: dist under the repo root).

.PARAMETER Repo
    Repository root (default: inferred from this script).

.PARAMETER SkipPublish
    Reuse an existing app publish instead of re-running dotnet publish.
#>
[CmdletBinding()]
param(
    [string]$Dest,
    [string]$Repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path,
    [switch]$SkipPublish
)
$ErrorActionPreference = 'Stop'

if (-not $Dest) { $Dest = Join-Path $Repo 'dist' }
$appOut = Join-Path $Dest 'app'
$drvOut = Join-Path $Dest 'driver'
$pkg    = Join-Path $Repo 'build_driver\pkg'
$bwin   = Join-Path $Repo 'build_win'

function Need($p) { if (-not (Test-Path $p)) { throw "required input missing: $p" } }

Need $pkg
Need (Join-Path $pkg 'semantics_ar.sys')
Need (Join-Path $pkg 'semantics_ar.cat')
Need (Join-Path $pkg 'semantics_ar_elam.sys')
Need (Join-Path $bwin 'frontend\sarapi\Release\sarapi.dll')
Need (Join-Path $bwin 'frontend\elevation-host\Release\SemanticsArElevationHost.exe')
Need (Join-Path $bwin 'service\Release\semantics_ar_service.exe')
Need (Join-Path $bwin 'tools\Release\sar_install.exe')

Write-Host "assembling payload -> $Dest" -ForegroundColor Cyan
if (Test-Path $Dest) { Remove-Item $Dest -Recurse -Force }
New-Item -ItemType Directory -Force -Path $appOut, $drvOut | Out-Null

# ── app: framework-dependent publish, then overlay the ABI-matched native surface ──────────────
if (-not $SkipPublish) {
    & dotnet publish (Join-Path $Repo 'frontend\SemanticsAr.App\SemanticsAr.App.csproj') `
        -c Release -o $appOut --nologo -v quiet
    if ($LASTEXITCODE -ne 0) { throw 'dotnet publish failed' }
}
Copy-Item (Join-Path $bwin 'frontend\sarapi\Release\sarapi.dll')                       $appOut -Force
Copy-Item (Join-Path $bwin 'frontend\elevation-host\Release\SemanticsArElevationHost.exe') $appOut -Force
Copy-Item (Join-Path $bwin 'frontend\elevation-host\Release\SemanticsArElevation.tlb')     $appOut -Force
Copy-Item (Join-Path $bwin 'service\Release\semantics_ar_service.exe')                  $appOut -Force
Copy-Item (Join-Path $bwin 'tools\Release\sar_install.exe')                             $appOut -Force

# ── driver: signed package outputs ────────────────────────────────────────────────────────────
foreach ($f in 'semantics_ar.sys', 'semantics_ar.inf', 'semantics_ar.cat', 'semantics_ar_elam.sys') {
    Copy-Item (Join-Path $pkg $f) $drvOut -Force
}
$cer = Join-Path $pkg 'SemanticsArTest.cer'
if (Test-Path $cer) { Copy-Item $cer $drvOut -Force }

Copy-Item (Join-Path $PSScriptRoot 'SemanticsAr-Setup.ps1') $Dest -Force

$appExe = Join-Path $appOut 'SemanticsAr.App.exe'
if (-not (Test-Path $appExe)) { throw "publish did not produce SemanticsAr.App.exe" }

Write-Host "`npayload ready:" -ForegroundColor Green
Write-Host ("  app files   : {0}" -f (Get-ChildItem $appOut -Recurse -File).Count)
Write-Host ("  driver files: {0}" -f (Get-ChildItem $drvOut -File).Count)
Write-Host "`ninstall on target (elevated):"
Write-Host "  powershell -NoProfile -ExecutionPolicy Bypass -File SemanticsAr-Setup.ps1 -Action Install"
