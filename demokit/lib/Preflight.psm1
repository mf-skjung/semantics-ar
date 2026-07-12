# Preflight.psm1 - read-only environment detection for the Demo Kit.
# NOTHING here mutates the system. Every probe reports the CURRENT reality so the
# orchestrator can skip work that is already done (idempotency) and refuse on hard blocks.

function Test-Elevated {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p = New-Object Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# Returns @{ IsVm; Manufacturer; Model; Hypervisor; PartOfDomain }
function Get-MachineIdentity {
    $cs = Get-CimInstance Win32_ComputerSystem -ErrorAction SilentlyContinue
    $mf = if ($cs) { [string]$cs.Manufacturer } else { '' }
    $md = if ($cs) { [string]$cs.Model } else { '' }
    $domain = if ($cs) { [bool]$cs.PartOfDomain } else { $false }

    # Hypervisor allowlist matched against manufacturer/model strings.
    $hv = $null
    $probe = "$mf $md"
    switch -Regex ($probe) {
        'Microsoft.*Virtual|Hyper-V'        { $hv = 'Microsoft Hyper-V'; break }
        'VMware'                            { $hv = 'VMware'; break }
        'innotek|VirtualBox|Oracle'         { $hv = 'VirtualBox'; break }
        'QEMU|KVM|Bochs'                    { $hv = 'QEMU/KVM'; break }
        'Xen'                               { $hv = 'Xen'; break }
        'Google'                            { $hv = 'Google Compute Engine'; break }
        'Amazon|EC2'                        { $hv = 'Amazon EC2'; break }
        'Parallels'                         { $hv = 'Parallels'; break }
    }
    # Secondary signal: HypervisorPresent (true inside most guests, but also on hosts
    # running Hyper-V, so it only strengthens a positive manufacturer match).
    return [pscustomobject]@{
        IsVm         = [bool]$hv
        Manufacturer = $mf
        Model        = $md
        Hypervisor   = $hv
        PartOfDomain = $domain
    }
}

# $true = ON, $false = OFF/not-applicable. Confirm-SecureBootUEFI throws on BIOS/Gen1
# firmware; that is the GOOD case (no Secure Boot to worry about) and maps to OFF.
function Test-SecureBootOn {
    try {
        return [bool](Confirm-SecureBootUEFI -ErrorAction Stop)
    } catch {
        return $false
    }
}

function Test-TestSigningOn {
    try {
        $out = & bcdedit /enum '{current}' 2>$null | Out-String
        return ($out -match '(?im)^\s*testsigning\s+Yes\s*$')
    } catch {
        return $false
    }
}

# HVCI / Memory Integrity running now. Uses the DeviceGuard CIM class (authoritative for
# "running") and falls back to the policy registry key.
function Test-HvciOn {
    try {
        $dg = Get-CimInstance -Namespace 'root\Microsoft\Windows\DeviceGuard' -ClassName Win32_DeviceGuard -ErrorAction Stop
        if ($dg -and ($dg.SecurityServicesRunning -contains 2)) { return $true }
        if ($dg -and ($dg.SecurityServicesConfigured -contains 2)) { return $true }
    } catch { }
    $reg = 'HKLM:\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity'
    try {
        $v = (Get-ItemProperty -Path $reg -Name Enabled -ErrorAction Stop).Enabled
        return ($v -eq 1)
    } catch {
        return $false
    }
}

function Get-DiskFreeGB {
    param([string]$Drive = $env:SystemDrive)
    try {
        $d = Get-PSDrive -Name ($Drive.TrimEnd(':', '\')) -ErrorAction Stop
        return [math]::Round($d.Free / 1GB, 1)
    } catch {
        return 0
    }
}

# Verify payload files against the hashes recorded in KIT-VERSION.json (tamper / partial-copy guard).
# Returns @{ Ok; Missing = @(); Mismatch = @() }
function Test-PayloadIntegrity {
    param([Parameter(Mandatory)][string]$KitRoot)
    $verPath = Join-Path $KitRoot 'KIT-VERSION.json'
    $result = [pscustomobject]@{ Ok = $false; Missing = @(); Mismatch = @(); Reason = '' }
    if (-not (Test-Path $verPath)) { $result.Reason = 'KIT-VERSION.json missing'; return $result }

    $ver = Get-Content $verPath -Raw | ConvertFrom-Json
    if (-not $ver.payload) { $result.Reason = 'no payload hashes in KIT-VERSION.json'; return $result }

    foreach ($prop in $ver.payload.PSObject.Properties) {
        $rel = $prop.Name
        $expected = ([string]$prop.Value) -replace '^sha256:', ''
        $full = Join-Path $KitRoot $rel
        if (-not (Test-Path $full)) { $result.Missing += $rel; continue }
        $actual = (Get-FileHash -Path $full -Algorithm SHA256).Hash
        if ($actual -ne $expected) { $result.Mismatch += $rel }
    }
    $result.Ok = (($result.Missing.Count -eq 0) -and ($result.Mismatch.Count -eq 0))
    if (-not $result.Ok -and -not $result.Reason) { $result.Reason = 'payload hash mismatch or missing files' }
    return $result
}

function Test-AppSelfContained {
    param([Parameter(Mandatory)][string]$KitRoot)
    return (Test-Path (Join-Path $KitRoot 'payload\app\.self-contained'))
}

# Aggregate probe. Returns an ordered list of check objects the UI renders and the
# orchestrator reasons over. Each: @{ Key; Status(GO/FIX/STOP); Name; Detail; Fixable }
function Invoke-Preflight {
    param([Parameter(Mandatory)][string]$KitRoot, [switch]$IAmSure)

    $checks = New-Object System.Collections.Generic.List[object]
    function Add($key, $status, $name, $detail, $fixable) {
        $checks.Add([pscustomobject]@{ Key = $key; Status = $status; Name = $name; Detail = $detail; Fixable = $fixable })
    }

    $elevated = Test-Elevated
    Add 'elevated' ($(if ($elevated) { 'GO' } else { 'STOP' })) 'Elevated (Administrator)' `
        ($(if ($elevated) { '' } else { 're-run elevated (the kit self-elevates via UAC)' })) $false

    $mach = Get-MachineIdentity
    if ($mach.IsVm) {
        Add 'vm' 'GO' 'Running inside a virtual machine' $mach.Hypervisor $false
    } elseif ($IAmSure) {
        Add 'vm' 'GO' 'Throwaway-machine override (-IAmSure)' ("host: {0} {1}" -f $mach.Manufacturer, $mach.Model) $false
    } else {
        Add 'vm' 'STOP' 'This does not look like a virtual machine' `
            ("Manufacturer: {0}. Use a throwaway VM, or pass -IAmSure if certain." -f $mach.Manufacturer) $false
    }

    if ($mach.PartOfDomain -and -not $IAmSure) {
        Add 'domain' 'STOP' 'Machine is domain-joined' 'managed/corporate machines are not throwaway; pass -IAmSure to override' $false
    }

    $free = Get-DiskFreeGB
    Add 'disk' ($(if ($free -ge 8) { 'GO' } else { 'STOP' })) ('Disk free {0} GB (need >= 8 GB)' -f $free) '' $false

    $ts = Test-TestSigningOn
    Add 'testsigning' ($(if ($ts) { 'GO' } else { 'FIX' })) ('Test signing ......... {0}' -f $(if ($ts) { 'ON' } else { 'OFF' })) `
        ($(if ($ts) { '' } else { 'will enable (needs reboot)' })) (-not $ts)

    $hvci = Test-HvciOn
    Add 'hvci' ($(if ($hvci) { 'FIX' } else { 'GO' })) ('Memory Integrity ..... {0}' -f $(if ($hvci) { 'ON' } else { 'OFF' })) `
        ($(if ($hvci) { 'will disable (needs reboot)' } else { '' })) $hvci

    $sb = Test-SecureBootOn
    if ($sb) {
        Add 'secureboot' 'STOP' 'Secure Boot .......... ON' 'FIRMWARE setting - cannot change from inside Windows. Disable it (or use a Gen1/BIOS VM) and re-run.' $false
    } else {
        Add 'secureboot' 'GO' 'Secure Boot .......... OFF' '(required OFF - good)' $false
    }

    $integ = Test-PayloadIntegrity -KitRoot $KitRoot
    Add 'payload' ($(if ($integ.Ok) { 'GO' } else { 'STOP' })) 'Product payload present + hash-verified' `
        ($(if ($integ.Ok) { '' } else { $integ.Reason })) $false

    $sc = Test-AppSelfContained -KitRoot $KitRoot
    Add 'selfcontained' 'INFO' ('App is self-contained ... {0}' -f $(if ($sc) { 'yes (no .NET runtime needed)' } else { 'no (needs .NET 10 Desktop Runtime)' })) '' $false

    return $checks
}

Export-ModuleMember -Function Test-Elevated, Get-MachineIdentity, Test-SecureBootOn, Test-TestSigningOn, Test-HvciOn, Get-DiskFreeGB, Test-PayloadIntegrity, Test-AppSelfContained, Invoke-Preflight
