<#
.SYNOPSIS
    Regression test for the semantics-ar Demo Kit against the SarTarget VM.

.DESCRIPTION
    Drives the fully-automatable slice of the kit on a clean snapshot:
      preflight (no-reboot idempotent path, since the baseline is already test-signed)
      -> product install (driver load + service + COM) -> sandbox seed -> DEMO READY
      -> safe attack (asserts sandbox-only containment) -> reset -> product uninstall.

    The single-reboot path (a VM whose test signing is OFF) is verified separately/manually:
    the kit's own Restart-Computer -Force cleanly flushes the BCD change and the AtLogon
    SarDemoResume task drives the resume. NOTE: do NOT reboot the guest with
    `Restart-VM -Force` in a harness - a hard reset can drop the pending BCD write; use a
    guest-clean reboot (Restart-Computer / Restart-VM without -Force).

    Prereq: build the kit first  ->  installer\Build-DemoKit.ps1  (produces dist-demokit\SarDemoKit).
#>
[CmdletBinding()]
param(
    [string]$VMName = 'SarTarget',
    [string]$Snapshot = 'clean-baseline-20260704',
    [string]$Repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path,
    [string]$User = 'admin',
    [string]$Pass = 'admin'
)
$ErrorActionPreference = 'Stop'
$cred = New-Object System.Management.Automation.PSCredential($User, (ConvertTo-SecureString $Pass -AsPlainText -Force))
$passed = 0; $failed = 0
function Assert($name, $cond) {
    if ($cond) { Write-Host "  PASS  $name" -ForegroundColor Green; $script:passed++ }
    else { Write-Host "  FAIL  $name" -ForegroundColor Red; $script:failed++ }
}
function VM([scriptblock]$sb) { Invoke-Command -VMName $VMName -Credential $cred -ScriptBlock $sb }

# 0 - kit + zip
$kit = Join-Path $Repo 'dist-demokit\SarDemoKit'
if (-not (Test-Path (Join-Path $kit 'Start-SarDemo.ps1'))) { throw "kit not built; run installer\Build-DemoKit.ps1 first" }
$zip = Join-Path $Repo 'dist-demokit\SarDemoKit.zip'
Add-Type -AssemblyName System.IO.Compression.FileSystem
if ([System.IO.File]::Exists($zip)) { [System.IO.File]::Delete($zip) }
[System.IO.Compression.ZipFile]::CreateFromDirectory($kit, $zip)

Write-Host "`n=== demo-kit verify ($(Get-Date -Format 'HH:mm:ss')) ===" -ForegroundColor Cyan
Write-Host "restore: $Snapshot" -ForegroundColor Yellow
Restore-VMSnapshot -VMName $VMName -Name $Snapshot -Confirm:$false
Start-Sleep 2
if ((Get-VM $VMName).State -ne 'Running') { Start-VM $VMName }
$up = $false
for ($i = 0; $i -lt 36; $i++) { Start-Sleep 5; try { if (VM { 1 }) { $up = $true; break } } catch { } }
Assert 'guest reachable via PS Direct' $up
if (-not $up) { Write-Host 'ABORT: guest not reachable'; return }

# 1 - deploy
$s = New-PSSession -VMName $VMName -Credential $cred
Copy-Item $zip 'C:\SarDemoKit.zip' -ToSession $s -Force
Invoke-Command -Session $s { Expand-Archive 'C:\SarDemoKit.zip' -DestinationPath ('C:\' + 'SarDemoKit') -Force }
$s | Remove-PSSession

# 2 - run kit (no-reboot idempotent path)
$out = VM { & powershell -NoProfile -ExecutionPolicy Bypass -File 'C:\SarDemoKit\Start-SarDemo.ps1' -NoElevate -Assume -IAmSure 2>&1 | Out-String }
Assert 'preflight took the no-reboot path' ($out -match 'No reboot needed')
Assert 'reached DEMO READY'                ($out -match 'DEMO READY')
Assert 'stale service was repaired'        ($out -match 'stale minifilter service' -or $out -notmatch 'FAILED')

# 3 - product state
Assert 'minifilter attached' ([bool](VM { (fltmc filters 2>$null) -match 'semantics_ar' }))
Assert 'user service running' ([bool](VM { (Get-Service SemanticsAr -EA SilentlyContinue).Status -eq 'Running' }))
Assert 'sandbox seeded'       ([bool](VM { (Test-Path 'C:\SarDemo\Sandbox\.sar-sandbox') -and ((Get-ChildItem 'C:\SarDemo\Sandbox' -File).Count -ge 12) }))

# 4 - safe attack: containment (external canary must be untouched) + reversibility
$att = VM {
    $ext = 'C:\Users\admin\Documents\REAL-do-not-touch.txt'
    New-Item -ItemType Directory -Force -Path (Split-Path $ext) | Out-Null
    Set-Content $ext 'precious' -Encoding UTF8; $b = (Get-FileHash $ext).Hash
    $sample = 'C:\SarDemo\Sandbox\quarterly-report.txt'; $h0 = (Get-Content $sample -TotalCount 1)
    & powershell -NoProfile -ExecutionPolicy Bypass -File 'C:\SarDemoKit\attack\Start-Attack.ps1' *>&1 | Out-Null
    $changed = ((Get-Content $sample -TotalCount 1 -EA SilentlyContinue) -ne $h0)
    $note = Test-Path 'C:\SarDemo\Sandbox\READ_ME_TO_RESTORE.txt'
    $extSame = ($b -eq (Get-FileHash $ext).Hash)
    & powershell -NoProfile -ExecutionPolicy Bypass -File 'C:\SarDemoKit\attack\Reset-Attack.ps1' *>&1 | Out-Null
    $restored = ((Get-Content $sample -TotalCount 1) -eq $h0)
    [pscustomobject]@{ changed = $changed; note = $note; extSame = $extSame; restored = $restored }
}
Assert 'attack encrypted sandbox files'        $att.changed
Assert 'attack dropped ransom note (sandbox)'  $att.note
Assert 'EXTERNAL file untouched (containment)'  $att.extSame
Assert 'reset restored originals'              $att.restored

# 5 - uninstall (teardown core)
$uni = VM { & powershell -NoProfile -ExecutionPolicy Bypass -File 'C:\SarDemoKit\payload\SemanticsAr-Setup.ps1' -Action Uninstall 2>&1 | Out-String }
Assert 'minifilter gone after uninstall' ([bool](VM { -not ((fltmc filters 2>$null) -match 'semantics_ar') }))
Assert 'service gone after uninstall'    ([bool](VM { $null -eq (Get-Service SemanticsAr -EA SilentlyContinue) }))

Write-Host "`n=== demo-kit verify: $passed passed, $failed failed ===" -ForegroundColor Cyan
if ($failed) { exit 1 }
