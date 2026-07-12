# State.psm1 - durable state + the post-reboot self-resume mechanism.
# State lives OUTSIDE the product install root so the product uninstall never touches it
# and it survives the reboot that activates test signing.

$script:StateRoot = 'C:\SarDemo'
$script:StatePath = Join-Path $script:StateRoot 'state.json'
$script:ResumeTask = 'SarDemoResume'

function Get-SarStateRoot { return $script:StateRoot }
function Get-SarStatePath { return $script:StatePath }

function Get-SarState {
    if (-not (Test-Path $script:StatePath)) { return $null }
    try {
        return (Get-Content $script:StatePath -Raw | ConvertFrom-Json)
    } catch {
        return $null
    }
}

# Merge a hashtable of fields into the persisted state and stamp it. Creates the root if needed.
function Save-SarState {
    param([Parameter(Mandatory)][hashtable]$Fields)
    if (-not (Test-Path $script:StateRoot)) { New-Item -ItemType Directory -Force -Path $script:StateRoot | Out-Null }

    $obj = @{}
    $existing = Get-SarState
    if ($existing) { foreach ($p in $existing.PSObject.Properties) { $obj[$p.Name] = $p.Value } }
    foreach ($k in $Fields.Keys) { $obj[$k] = $Fields[$k] }
    $obj['ts'] = (Get-Date).ToUniversalTime().ToString('o')

    ($obj | ConvertTo-Json -Depth 8) | Set-Content -Path $script:StatePath -Encoding UTF8
}

function Clear-SarState {
    if (Test-Path $script:StatePath) { Remove-Item $script:StatePath -Force -ErrorAction SilentlyContinue }
}

# Register a scheduled task that re-launches the orchestrator elevated at the next logon.
# AtLogon (not AtStartup) so the app opens into an interactive desktop; RunLevel Highest so
# the elevated install works without a UAC prompt inside the resumed session.
function Register-ResumeTask {
    param([Parameter(Mandatory)][string]$KitPath)

    $startScript = Join-Path $KitPath 'Start-SarDemo.ps1'
    $args = '-NoProfile -ExecutionPolicy Bypass -WindowStyle Normal -File "{0}" -Resume' -f $startScript
    $action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument $args
    $trigger = New-ScheduledTaskTrigger -AtLogOn
    $principal = New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().Name) -RunLevel Highest -LogonType Interactive
    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable -ExecutionTimeLimit ([TimeSpan]::FromHours(1))

    Unregister-ResumeTask
    Register-ScheduledTask -TaskName $script:ResumeTask -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Force | Out-Null
}

function Unregister-ResumeTask {
    try {
        if (Get-ScheduledTask -TaskName $script:ResumeTask -ErrorAction SilentlyContinue) {
            Unregister-ScheduledTask -TaskName $script:ResumeTask -Confirm:$false -ErrorAction SilentlyContinue
        }
    } catch { }
}

# Opt-in one-shot auto-logon so even the login after reboot is hands-free. Writes a plaintext
# DefaultPassword (throwaway VM only); the orchestrator clears it at READY. Returns $true on success.
function Set-OneShotAutoLogon {
    param([Parameter(Mandatory)][string]$UserName, [Parameter(Mandatory)][string]$Password)
    $key = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon'
    try {
        Set-ItemProperty -Path $key -Name 'AutoAdminLogon' -Value '1' -Type String
        Set-ItemProperty -Path $key -Name 'DefaultUserName' -Value $UserName -Type String
        Set-ItemProperty -Path $key -Name 'DefaultPassword' -Value $Password -Type String
        # AutoLogonCount is decremented by Winlogon each boot; 1 => single hands-free logon.
        Set-ItemProperty -Path $key -Name 'AutoLogonCount' -Value 1 -Type DWord
        return $true
    } catch {
        return $false
    }
}

function Clear-AutoLogon {
    $key = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon'
    foreach ($n in 'AutoAdminLogon', 'DefaultPassword', 'AutoLogonCount') {
        Remove-ItemProperty -Path $key -Name $n -ErrorAction SilentlyContinue
    }
}

Export-ModuleMember -Function Get-SarStateRoot, Get-SarStatePath, Get-SarState, Save-SarState, Clear-SarState, Register-ResumeTask, Unregister-ResumeTask, Set-OneShotAutoLogon, Clear-AutoLogon
