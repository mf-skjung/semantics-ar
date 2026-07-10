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
function Metric { param([string]$Name,[string]$Value) Write-Host ("  METRIC  {0,-42} {1}" -f $Name,$Value) -ForegroundColor Cyan }

Write-Host "`n=== XII attribution verification ($(Get-Date -Format 'HH:mm:ss')) ===" -ForegroundColor Cyan
Assert "minifilter live" ([bool](VM { [bool]((fltmc filters 2>$null) -match 'semantics_ar') }))
Assert "service running" ([bool](VM { (Get-Service semantics_ar_service -ErrorAction SilentlyContinue).Status -eq 'Running' }))
VM { & C:\sar\sarctl.exe mode audit 2>&1 | Out-Null; & C:\sar\sarctl.exe budget 604800 10240 2>&1 | Out-Null } | Out-Null

# ── Workload A: normal (non-mmap) no-key destruction -> BOUNDED preserves, causing = noninplace_destroyer.exe
Write-Host "`nWorkload A: renameover (normal seam) -> preserve + attribution" -ForegroundColor Yellow
$adir = "C:\sar\attr_norm"
VMArgs { param($d) if (Test-Path $d){ Remove-Item $d -Recurse -Force -ErrorAction SilentlyContinue }; New-Item -ItemType Directory $d -Force | Out-Null } @($adir)
VMArgs { param($d) & C:\sar\noninplace_destroyer.exe renameover $d 6 2>&1 | Out-Null } @($adir)
Start-Sleep -Seconds 3

# ── Workload B: mapped-section overwrite -> mmap seam preserves, causing = mmap_over.exe (arm-key path)
Write-Host "Workload B: mmap_over (mapped seam) -> preserve + attribution via arm-key" -ForegroundColor Yellow
$bdir = "C:\sar\attr_mmap"
VMArgs { param($d)
    if (Test-Path $d){ Remove-Item $d -Recurse -Force -ErrorAction SilentlyContinue }; New-Item -ItemType Directory $d -Force | Out-Null
    $b=[byte[]]::new(1048576); for($k=0;$k -lt $b.Length;$k++){ $b[$k]=[byte](65+($k%26)) }
    [IO.File]::WriteAllBytes("$d\sv_00000.dat",$b)
    $p = Start-Process C:\sar\mmap_over.exe -ArgumentList "$d\sv_00000.dat","3" -NoNewWindow -PassThru
    $p.WaitForExit(30000) | Out-Null
    $fs=[IO.File]::Open("$d\sv_00000.dat",'Open','ReadWrite','ReadWrite'); $fs.Flush($true); $fs.Close()
} @($bdir)
Start-Sleep -Seconds 3

# ── Read the elevated preserve list (via sarctl -> service control pipe -> attrib resolution) ──
$plist = VM { & C:\sar\sarctl.exe preserve-list 2>&1 | Out-String }
$applist = VM { & C:\sar\sarctl.exe app-identity 2>&1 | Out-String }
Write-Host "`n--- preserve-list (sample) ---" -ForegroundColor DarkGray
($plist -split "`n" | Where-Object { $_ -match 'app=' } | Select-Object -First 6) | ForEach-Object { Write-Host "  $_" }
Write-Host "--- app-identity ---" -ForegroundColor DarkGray
($applist -split "`n" | Where-Object { $_ -match '^\[app\]' }) | ForEach-Object { Write-Host "  $_" }

# Parse preserve entries
$entries = [regex]::Matches($plist, 'actor=(\d+) pool=(\d+) app=(\d+)')
$total = $entries.Count
$actorNonZero = 0; $appNonZero = 0; $poolValid = 0
$appIds = @{}
foreach ($m in $entries) {
    if ([uint64]$m.Groups[1].Value -ne 0) { $actorNonZero++ }
    $pool = [int]$m.Groups[2].Value; if ($pool -eq 0 -or $pool -eq 1) { $poolValid++ }
    $aid = $m.Groups[3].Value
    if ([uint64]$aid -ne 0) { $appNonZero++; $appIds[$aid] = $true }
}
Metric "preserve entries (actor/pool/app parsed)" "$total"
Metric "actor!=0 / pool in {0,1} / app!=0" "$actorNonZero / $poolValid / $appNonZero"

# Parse app-identity rows
$apps = [regex]::Matches($applist, '(?m)^\[app\] id=(\d+) verdict=(\d+) signer="([^"]*)"\s+(.+?)\s*$')
$appById = @{}
foreach ($m in $apps) { $appById[$m.Groups[1].Value] = @{ verdict=$m.Groups[2].Value; signer=$m.Groups[3].Value; path=$m.Groups[4].Value } }
Metric "app-identity rows" "$($apps.Count)"

$resolvesNorm = ($appById.Values | Where-Object { $_.path -match 'noninplace_destroyer\.exe' }).Count
$resolvesMmap = ($appById.Values | Where-Object { $_.path -match 'mmap_over\.exe' }).Count
Metric "app rows resolving to noninplace / mmap_over" "$resolvesNorm / $resolvesMmap"

# Cross-join: every app id referenced by a preserve entry must exist in the app-identity table
$joinMissing = 0
foreach ($aid in $appIds.Keys) { if (-not $appById.ContainsKey($aid)) { $joinMissing++ } }

Assert "XII.4: preserve entries carry a nonzero start-key actor" ($actorNonZero -ge 1) "actor!=0 count=$actorNonZero of $total"
Assert "XII.1: preserve entries carry a valid pool (probation/protected)" ($poolValid -eq $total -and $total -ge 1) "poolValid=$poolValid/$total"
Assert "XII.2: preserve entries carry a resolved app_identity_id" ($appNonZero -ge 1) "app!=0 count=$appNonZero of $total"
Assert "XII.2: normal-seam causing image resolves (noninplace_destroyer.exe)" ($resolvesNorm -ge 1) "resolvesNorm=$resolvesNorm"
Assert "XII.2/XII.4.2: mmap-seam causing image resolves via arm-key (mmap_over.exe)" ($resolvesMmap -ge 1) "resolvesMmap=$resolvesMmap"
Assert "XII.2: every preserve app_identity_id joins the identity table (no orphan)" ($joinMissing -eq 0) "orphans=$joinMissing"

Write-Host "`n=== attribution verification: $pass passed, $fail failed ===" -ForegroundColor Cyan
if ($fail -gt 0) { exit 1 }
