param(
    [string]$Out  = (Join-Path $PSScriptRoot "..\build_driver"),
    [string]$Repo = (Join-Path $PSScriptRoot ".."),
    [string]$CertSubject = "CN=SemanticsAr Test",
    [string]$OsList = "10_X64,10_GE_X64"
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
if (-not $signtool) { throw "signtool.exe not found in the Windows Kit" }
if (-not $inf2cat)  { throw "Inf2Cat.exe not found in the Windows Kit" }

$pkg = Join-Path $Out 'pkg'
New-Item -ItemType Directory -Force -Path $pkg | Out-Null
Copy-Item (Join-Path $Out 'semantics_ar.sys')          $pkg -Force
Copy-Item (Join-Path $Repo 'driver\semantics_ar.inf')  $pkg -Force

$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq $CertSubject } | Select-Object -First 1
if (-not $cert) {
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $CertSubject `
        -CertStoreLocation Cert:\CurrentUser\My -KeyUsage DigitalSignature `
        -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3")
}
Export-Certificate -Cert $cert -FilePath (Join-Path $pkg 'SemanticsArTest.cer') | Out-Null

& $signtool sign /v /fd sha256 /sha1 $cert.Thumbprint (Join-Path $pkg 'semantics_ar.sys')
& $inf2cat /driver:$pkg /os:$OsList
& $signtool sign /v /fd sha256 /sha1 $cert.Thumbprint (Join-Path $pkg 'semantics_ar.cat')

Write-Host "`nPackage ready at $pkg :"
Get-ChildItem $pkg | Select-Object Name, Length
Write-Host "cert thumbprint = $($cert.Thumbprint)"
