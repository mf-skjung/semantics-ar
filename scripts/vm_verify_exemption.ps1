param(
    [string]$VMName = "SarTarget",
    [PSCredential]$Credential
)
$ErrorActionPreference = 'Stop'
if (-not $Credential) {
    $pw = ConvertTo-SecureString "admin" -AsPlainText -Force
    $Credential = New-Object System.Management.Automation.PSCredential("admin", $pw)
}
$script:Sess = $null
function Connect-VM {
    if ($script:Sess -and $script:Sess.State -eq 'Opened') { return }
    if ($script:Sess) { Remove-PSSession $script:Sess -ErrorAction SilentlyContinue; $script:Sess = $null }
    for ($i = 0; $i -lt 10; $i++) {
        try { $script:Sess = New-PSSession -VMName $VMName -Credential $Credential -ErrorAction Stop; return }
        catch { Start-Sleep -Seconds 5 }
    }
    throw "Cannot open PowerShell Direct session to VM '$VMName'."
}
function VM { param([scriptblock]$Script) Connect-VM; Invoke-Command -Session $script:Sess -ScriptBlock $Script }
function VMArgs { param([scriptblock]$Script,[object[]]$Arguments) Connect-VM; Invoke-Command -Session $script:Sess -ScriptBlock $Script -ArgumentList $Arguments }

$pass = 0; $fail = 0
function Assert { param([string]$Name,[bool]$Cond,[string]$Detail="")
    if ($Cond) { Write-Host "  PASS  $Name" -ForegroundColor Green; $script:pass++ }
    else { Write-Host "  FAIL  $Name  $Detail" -ForegroundColor Red; $script:fail++ }
}
function Metric { param([string]$Name,[string]$Value) Write-Host ("  METRIC  {0,-40} {1}" -f $Name,$Value) -ForegroundColor Cyan }

Write-Host "`n=== XII.5 exemption enumerate verification ($(Get-Date -Format 'HH:mm:ss')) ===" -ForegroundColor Cyan
Assert "minifilter live" ([bool](VM { [bool]((fltmc filters 2>$null) -match 'semantics_ar') }))
Assert "service running" ([bool](VM { (Get-Service semantics_ar_service -ErrorAction SilentlyContinue).Status -eq 'Running' }))

# sar_identity_evaluate requires an EMBEDDED Authenticode signature chaining to a trusted root.
# Windows binaries are catalog-signed (no embedded sig) -> UNSIGNED. So mint verified probe
# binaries by embedding a test-cert signature (the test cert is in the VM's Root store), one with
# an ordinary name and one named like an interpreter (leaf-name is what VI.2.4 keys on).
$signtool = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq "CN=SemanticsAr Test" } | Select-Object -First 1
$srcExe = Join-Path $PSScriptRoot "..\build_win\tools\Release\sarctl.exe"
$appLocal = Join-Path $env:TEMP "sar_probe_app.exe"
$interpLocal = Join-Path $env:TEMP "powershell.exe"
Copy-Item $srcExe $appLocal -Force
Copy-Item $srcExe $interpLocal -Force
& $signtool sign /fd sha256 /sha1 $cert.Thumbprint $appLocal 2>&1 | Out-Null
& $signtool sign /fd sha256 /sha1 $cert.Thumbprint $interpLocal 2>&1 | Out-Null
Connect-VM
Copy-Item $appLocal -Destination "C:\sar\probe_app.exe" -ToSession $script:Sess -Force
Copy-Item $interpLocal -Destination "C:\sar\probe_interp_powershell.exe" -ToSession $script:Sess -Force
# The interpreter leaf-name check matches the exact filename, so stage the signed copy under the
# reserved name in its own directory.
VM { New-Item -ItemType Directory -Force "C:\sar\interp" | Out-Null; Copy-Item "C:\sar\probe_interp_powershell.exe" "C:\sar\interp\powershell.exe" -Force }

$signed = "C:\sar\probe_app.exe"
$interp = "C:\sar\interp\powershell.exe"

# Clean slate.
VMArgs { param($p) & C:\sar\sarctl.exe whitelist-remove $p 2>&1 | Out-Null } @($signed)
Start-Sleep -Seconds 1

# ── Add a verified-signed, non-interpreter app ──
$add = VMArgs { param($p) & C:\sar\sarctl.exe whitelist-add $p 2>&1 | Out-String } @($signed)
Metric "whitelist-add(signed)" ($add.Trim())
Start-Sleep -Seconds 1
$list1 = VM { & C:\sar\sarctl.exe whitelist-list 2>&1 | Out-String }
Write-Host "--- whitelist-list ---" -ForegroundColor DarkGray
($list1 -split "`n" | Where-Object { $_ -match '^\[wl\]|exemption' }) | ForEach-Object { Write-Host "  $_" }

Assert "XII.5: add of verified-signed app succeeds" ($add -match 'result=0 verdict=verified')
Assert "XII.5: signed app is enumerated" ($list1 -match 'probe_app\.exe')
Assert "XII.5: freshly-added app reports match=matching" ($list1 -match 'match=matching')
Assert "XII.5: first_seen timestamp is nonzero" ($list1 -match 'first_seen=[1-9]\d*')
Assert "XII.5: signer subject is captured" ($list1 -match 'signer="SemanticsAr Test"')

# ── A verified-signed INTERPRETER must be refused at add-time and never enumerated (XII.5.1 / VI.2.4) ──
$addi = VMArgs { param($p) & C:\sar\sarctl.exe whitelist-add $p 2>&1 | Out-String } @($interp)
Metric "whitelist-add(signed interpreter)" ($addi.Trim())
Start-Sleep -Seconds 1
$list2 = VM { & C:\sar\sarctl.exe whitelist-list 2>&1 | Out-String }
Assert "XII.5.1: verified interpreter add refused (result=-100)" ($addi -match 'result=-100')
Assert "XII.5.1: interpreter never enumerated as exemptable" (-not ($list2 -match 'powershell\.exe'))

# A trailing space/dot is stripped by Win32 when opening, so the file is the real interpreter; the
# leaf-name refusal must trim trailing spaces/dots or it is bypassed.
$addt = VMArgs { param($p) & C:\sar\sarctl.exe whitelist-add $p 2>&1 | Out-String } @("C:\sar\interp\powershell.exe ")
Metric "whitelist-add(interpreter trailing-space)" ($addt.Trim())
Start-Sleep -Seconds 1
$list2b = VM { & C:\sar\sarctl.exe whitelist-list 2>&1 | Out-String }
Assert "XII.5.1: trailing-space interpreter still refused (result=-100)" ($addt -match 'result=-100')
Assert "XII.5.1: trailing-space interpreter never enumerated" (-not ($list2b -match 'powershell\.exe'))

# ── Remove a matching exemption ──
$rem = VMArgs { param($p) & C:\sar\sarctl.exe whitelist-remove $p 2>&1 | Out-String } @($signed)
Metric "whitelist-remove(signed)" ($rem.Trim())
Start-Sleep -Seconds 1
$list3 = VM { & C:\sar\sarctl.exe whitelist-list 2>&1 | Out-String }
Assert "XII.5: matching exemption is removable by path" (-not ($list3 -match 'probe_app\.exe'))

Write-Host "`n=== exemption verification: $pass passed, $fail failed ===" -ForegroundColor Cyan
if ($fail -gt 0) { exit 1 }
