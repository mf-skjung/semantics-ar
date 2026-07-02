param(
    [string]$Out  = (Join-Path $PSScriptRoot "..\build_driver"),
    [string]$Repo = (Join-Path $PSScriptRoot ".."),
    [string]$CertSubject = "CN=SemanticsAr Test",
    [string]$OsList = "10_X64,10_GE_X64",
    [string]$ServiceExe = (Join-Path $PSScriptRoot "..\build_win\service\Release\semantics_ar_service.exe"),
    [string]$InstallExe = (Join-Path $PSScriptRoot "..\build_win\tools\Release\sar_install.exe")
)
$ErrorActionPreference = 'Stop'
$Out  = (Resolve-Path $Out).Path
$Repo = (Resolve-Path $Repo).Path

function Find-KitTool([string]$arch, [string]$name) {
    Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin\*\$arch\$name" -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Select-Object -First 1 -Expand FullName
}
$signtool = Find-KitTool 'x64' 'signtool.exe'
$inf2cat  = Find-KitTool 'x86' 'Inf2Cat.exe'
$rcExe    = Find-KitTool 'x64' 'rc.exe'
if (-not $signtool) { throw "signtool.exe not found in the Windows Kit" }
if (-not $inf2cat)  { throw "Inf2Cat.exe not found in the Windows Kit" }
if (-not $rcExe)    { throw "rc.exe not found in the Windows Kit" }

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath  = & $vswhere -latest -property installationPath
$vcvars  = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"

$pkg = Join-Path $Out 'pkg'
New-Item -ItemType Directory -Force -Path $pkg | Out-Null
Copy-Item (Join-Path $Out 'semantics_ar.sys')          $pkg -Force
Copy-Item (Join-Path $Repo 'driver\semantics_ar.inf')  $pkg -Force

$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq $CertSubject } | Select-Object -First 1
if (-not $cert) {
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $CertSubject `
        -CertStoreLocation Cert:\CurrentUser\My -KeyUsage DigitalSignature `
        -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3,1.3.6.1.4.1.311.61.4.1")
}
Export-Certificate -Cert $cert -FilePath (Join-Path $pkg 'SemanticsArTest.cer') | Out-Null

$certHashHex = ([System.Security.Cryptography.SHA256]::Create().ComputeHash($cert.RawData) |
    ForEach-Object { $_.ToString('X2') }) -join ''

$elamObj = Join-Path $Out 'elam.obj'
if (-not (Test-Path $elamObj)) { throw "elam.obj not found at $elamObj; run build_driver.bat first" }

$elamRcPath = Join-Path $pkg 'elam.rc'
@"
MicrosoftElamCertificateInfo MSElamCertInfoID
{
    1,
    L"$certHashHex\0",
    0x800c,
    L"\0",
}
"@ | Set-Content -Path $elamRcPath -Encoding ASCII

$elamRes = Join-Path $pkg 'elam.res'
$elamSys = Join-Path $pkg 'semantics_ar_elam.sys'
$buildCmds = @"
call "$vcvars" >nul
set LIB=%VCToolsInstallDir%lib\x64;%WindowsSdkDir%Lib\%WindowsSDKVersion%km\x64;%WindowsSdkDir%Lib\%WindowsSDKVersion%ucrt\x64
"$rcExe" /nologo /fo "$elamRes" "$elamRcPath"
link /nologo /DRIVER /INTEGRITYCHECK /SUBSYSTEM:NATIVE,10.00 /ENTRY:DriverEntry /NODEFAULTLIB /OUT:"$elamSys" "$elamObj" "$elamRes" ntoskrnl.lib hal.lib
"@
$buildScript = Join-Path $pkg 'build_elam.bat'
Set-Content -Path $buildScript -Value $buildCmds -Encoding ASCII
& cmd /c "`"$buildScript`""
if (-not (Test-Path $elamSys)) { throw "semantics_ar_elam.sys was not produced" }

& $signtool sign /v /fd sha256 /sha1 $cert.Thumbprint (Join-Path $pkg 'semantics_ar.sys')
& $signtool sign /v /fd sha256 /sha1 $cert.Thumbprint $elamSys

& $inf2cat /driver:$pkg /os:$OsList
& $signtool sign /v /fd sha256 /sha1 $cert.Thumbprint (Join-Path $pkg 'semantics_ar.cat')

if (Test-Path $ServiceExe) {
    Copy-Item $ServiceExe $pkg -Force
    & $signtool sign /v /fd sha256 /sha1 $cert.Thumbprint (Join-Path $pkg 'semantics_ar_service.exe')
} else {
    Write-Warning "service exe not found at $ServiceExe; skipping (build it, then re-run this script)"
}

if (Test-Path $InstallExe) {
    Copy-Item $InstallExe $pkg -Force
} else {
    Write-Warning "sar_install.exe not found at $InstallExe; skipping (build it, then re-run this script)"
}

Write-Host "`nPackage ready at $pkg :"
Get-ChildItem $pkg | Select-Object Name, Length
Write-Host "cert thumbprint = $($cert.Thumbprint)"
Write-Host "after install, run: sar_install.exe <path to semantics_ar_elam.sys>"
