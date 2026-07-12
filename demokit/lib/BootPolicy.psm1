# BootPolicy.psm1 - the ONLY boot-policy mutations in the kit. Each function is idempotent
# (checks current reality first and no-ops if already satisfied) and records prior state so
# teardown restores exactly what was changed. These require elevation; the orchestrator
# guarantees it before calling.

# BootPolicy calls Preflight's read-only probes. Import Preflight only if it is not already
# loaded - a -Force re-import here would unload it from the orchestrator's scope mid-run.
if (-not (Get-Command Test-TestSigningOn -ErrorAction SilentlyContinue)) {
    Import-Module (Join-Path $PSScriptRoot 'Preflight.psm1')
}

# Enable test signing. Idempotent. Returns @{ Changed; AlreadyOn }
function Enable-TestSigning {
    if (Test-TestSigningOn) { return @{ Changed = $false; AlreadyOn = $true } }
    & bcdedit /set '{current}' testsigning on | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "bcdedit could not enable testsigning (exit $LASTEXITCODE)" }
    return @{ Changed = $true; AlreadyOn = $false }
}

function Disable-TestSigning {
    if (-not (Test-TestSigningOn)) { return @{ Changed = $false } }
    & bcdedit /set '{current}' testsigning off | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "bcdedit could not disable testsigning (exit $LASTEXITCODE)" }
    return @{ Changed = $true }
}

$script:HvciKey = 'HKLM:\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity'

# Disable Memory Integrity (HVCI). Idempotent. Returns @{ Changed; WasOn }
function Disable-Hvci {
    $wasOn = Test-HvciOn
    if (-not $wasOn) { return @{ Changed = $false; WasOn = $false } }
    if (-not (Test-Path $script:HvciKey)) { New-Item -Path $script:HvciKey -Force | Out-Null }
    Set-ItemProperty -Path $script:HvciKey -Name 'Enabled' -Value 0 -Type DWord
    # WasEnabledBy, if present, keeps Windows from silently re-enabling; leave it, only Enabled=0 is needed.
    return @{ Changed = $true; WasOn = $true }
}

# Restore HVCI to on, used by teardown only when the kit was the one that turned it off.
function Restore-Hvci {
    if (-not (Test-Path $script:HvciKey)) { New-Item -Path $script:HvciKey -Force | Out-Null }
    Set-ItemProperty -Path $script:HvciKey -Name 'Enabled' -Value 1 -Type DWord
    return @{ Changed = $true }
}

Export-ModuleMember -Function Enable-TestSigning, Disable-TestSigning, Disable-Hvci, Restore-Hvci
