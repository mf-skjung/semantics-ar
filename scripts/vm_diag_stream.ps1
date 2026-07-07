param([string]$VMName="SarTarget",[string]$Repo=(Join-Path $PSScriptRoot ".."))
$ErrorActionPreference='Stop'; $Repo=(Resolve-Path $Repo).Path
$pw=ConvertTo-SecureString "admin" -AsPlainText -Force
$cred=New-Object System.Management.Automation.PSCredential("admin",$pw)
$sess=New-PSSession -VMName $VMName -Credential $cred
function CopyTo($l,$r){ Copy-Item -Path $l -Destination $r -ToSession $sess -Force }
function Inv($sb,$argl){ Invoke-Command -Session $sess -ScriptBlock $sb -ArgumentList $argl }
# timeout-guarded remote op: returns $null on hang
function InvT($sb,$argl,$sec){ $j=Invoke-Command -Session $sess -ScriptBlock $sb -ArgumentList $argl -AsJob; if(Wait-Job $j -Timeout $sec){ Receive-Job $j } else { Stop-Job $j; "HANG(${sec}s)" } }

Write-Host "=== deploy ===" -ForegroundColor Yellow
$pkg="$Repo\build_driver\pkg"
try { Invoke-Command -Session $sess { Stop-Service semantics_ar_service -Force -ErrorAction SilentlyContinue; fltmc unload semantics_ar 2>$null } } catch {}
Start-Sleep 2
Invoke-Command -Session $sess { if(-not(Test-Path "C:\sar")){New-Item -ItemType Directory "C:\sar" -Force|Out-Null} }
foreach($f in @("semantics_ar.sys","semantics_ar.inf","semantics_ar.cat","SemanticsArTest.cer")){ CopyTo "$pkg\$f" "C:\sar\$f" }
CopyTo "$Repo\build\service\Release\semantics_ar_service.exe" "C:\sar\semantics_ar_service.exe"
CopyTo "$Repo\build\tools\Release\sarctl.exe" "C:\sar\sarctl.exe"
CopyTo "$Repo\build_harness\stream_transform.exe" "C:\sar\stream_transform.exe"
Invoke-Command -Session $sess {
  $cer="C:\sar\SemanticsArTest.cer"; certutil -addstore Root $cer 2>$null|Out-Null; certutil -addstore TrustedPublisher $cer 2>$null|Out-Null
  pnputil /add-driver C:\sar\semantics_ar.inf /install 2>$null|Out-Null
  $svc="HKLM:\SYSTEM\CurrentControlSet\Services\semantics_ar"; New-Item $svc -Force|Out-Null
  Set-ItemProperty $svc ImagePath "\??\C:\sar\semantics_ar.sys"; Set-ItemProperty $svc Type 2 -Type DWord
  Set-ItemProperty $svc Start 3 -Type DWord; Set-ItemProperty $svc ErrorControl 1 -Type DWord
  Set-ItemProperty $svc Group "FSFilter Activity Monitor"; New-Item "$svc\Instances" -Force|Out-Null
  Set-ItemProperty "$svc\Instances" DefaultInstance "semantics_ar Instance"; New-Item "$svc\Instances\semantics_ar Instance" -Force|Out-Null
  Set-ItemProperty "$svc\Instances\semantics_ar Instance" Altitude "385000"; Set-ItemProperty "$svc\Instances\semantics_ar Instance" Flags 0 -Type DWord
  fltmc load semantics_ar 2>&1 | Out-Null
  Start-Sleep 2
  $p="C:\sar\semantics_ar_service.exe"; if(-not(Get-Service semantics_ar_service -ErrorAction SilentlyContinue)){New-Service -Name semantics_ar_service -BinaryPathName $p -StartupType Manual|Out-Null}
  Start-Service semantics_ar_service -ErrorAction SilentlyContinue; Start-Sleep 3
  "loaded=$([bool]((fltmc filters 2>$null) -match 'semantics_ar')) svc=$((Get-Service semantics_ar_service).Status)"
}

# H1 confirmation. Fill a small ENFORCE store with process A (a few large in-place overwrites), then have an
# UNRELATED process B do an in-place overwrite (contagion?), then RAISE the budget so the store has room and
# have B overwrite again (permanence? a process refused once stays denied even after room appears).
Write-Host "=== H1 confirm: contagion + permanence ===" -ForegroundColor Yellow
Invoke-Command -Session $sess {
  & C:\sar\sarctl.exe mode enforce 2>&1|Out-Null; & C:\sar\sarctl.exe budget 604800 4 2>&1|Out-Null
  $d="C:\sar\h1"; New-Item -ItemType Directory $d -Force|Out-Null
  # fill corpus (sv_*, overwritten in place by a SEPARATE stream_transform process) + B's two victims
  $big=[byte[]]::new(1048576); for($k=0;$k -lt $big.Length;$k++){$big[$k]=[byte]($k%251)}
  for($i=0;$i -lt 6;$i++){ [IO.File]::WriteAllBytes(("$d\sv_{0:D5}.dat" -f $i),$big) }
  [IO.File]::WriteAllBytes("$d\b1.dat",$big); [IO.File]::WriteAllBytes("$d\b2.dat",$big)
  $bscript = @'
param($f1,$f2,$go2,$d1,$d2)
function IPWrite($p){ try { $fs=[IO.File]::Open($p,'Open','Write','None'); $b=[byte[]]::new(1048576); for($k=0;$k -lt $b.Length;$k++){$b[$k]=[byte](($k*7)%251)}; $fs.Write($b,0,$b.Length); $fs.Flush($true); $fs.Close(); return "OK" } catch { return "DENIED:" + $_.Exception.GetType().Name } }
Set-Content $d1 (IPWrite $f1)
$t=0; while(-not (Test-Path $go2)){ Start-Sleep -Milliseconds 200; $t++; if($t -gt 300){break} }
Set-Content $d2 (IPWrite $f2)
'@
  Set-Content "$d\b.ps1" $bscript
}
# A fills the store via a SEPARATE process (never the session): 6 x 1MB in-place overwrites > 4MB budget.
# Guarded so the session is unaffected even if the driver misbehaves.
$fillOut = InvT {
  $d="C:\sar\h1"
  $p = Start-Process C:\sar\stream_transform.exe -ArgumentList @("chacha","20","oneshot",$d,"6","0") -NoNewWindow -PassThru
  if($p.WaitForExit(45000)){ $ex="exit=$($p.ExitCode)" } else { $p.Kill(); $ex="fill HANG" }
  Start-Sleep 3
  "store filled ($ex): block-capacity=$(((& C:\sar\sarctl.exe events 256 2>&1)|Select-String 'block-capacity').Count) preserve=$(((& C:\sar\sarctl.exe preserve-list 2>&1)|Select-String 'preserved region'))"
} @() 70
Write-Host "  $fillOut"
# B step 1: unrelated process overwrites b1 (store full) -> contagion?
Invoke-Command -Session $sess {
  $d="C:\sar\h1"
  Start-Process powershell -ArgumentList @("-NoProfile","-File","$d\b.ps1","$d\b1.dat","$d\b2.dat","$d\go2.txt","$d\d1.txt","$d\d2.txt") -WindowStyle Hidden
  $t=0; while(-not (Test-Path "$d\d1.txt")){ Start-Sleep -Milliseconds 300; $t++; if($t -gt 60){break} }
  "B step1 (store full): b1_result=$(if(Test-Path "$d\d1.txt"){Get-Content "$d\d1.txt"}else{'TIMEOUT'})"
} | ForEach-Object { Write-Host "  $_" }
# raise budget so the store has room, then B step 2
Invoke-Command -Session $sess {
  $d="C:\sar\h1"
  & C:\sar\sarctl.exe budget 604800 10240 2>&1|Out-Null
  Start-Sleep 2
  Set-Content "$d\go2.txt" "go"
  $t=0; while(-not (Test-Path "$d\d2.txt")){ Start-Sleep -Milliseconds 300; $t++; if($t -gt 60){break} }
  "B step2 (budget raised to 10GB, room available): b2_result=$(if(Test-Path "$d\d2.txt"){Get-Content "$d\d2.txt"}else{'TIMEOUT'})"
} | ForEach-Object { Write-Host "  $_" }
Write-Host "=== post-test driver liveness ===" -ForegroundColor Yellow
$r3 = InvT { "loaded=$([bool]((fltmc filters 2>$null) -match 'semantics_ar')) status=$((& C:\sar\sarctl.exe status 2>&1) -join ' ')" } @() 30
Write-Host "  $r3"
Remove-PSSession $sess
