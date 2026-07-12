# Ui.psm1 - console presentation for the semantics-ar Demo Kit.
# Pure output; no system mutation. Every phase reprints the TEST MODE banner so the
# disclaimer is present on every screen and screenshot.

$script:C = @{
    Ok    = 'Green'
    Fix   = 'Yellow'
    Stop  = 'Red'
    Info  = 'Gray'
    Head  = 'Cyan'
    Warn  = 'Yellow'
    Faint = 'DarkGray'
    Mode  = 'Magenta'
}

function Write-SarBanner {
    param(
        [string]$KitVersion = 'unknown',
        [string]$ProductCommit = 'unknown',
        [bool]$Dirty = $false,
        [string]$HostLine = ''
    )
    $dirtyTag = if ($Dirty) { ' (uncommitted)' } else { '' }
    $bar = '=' * 64
    Write-Host ''
    Write-Host $bar -ForegroundColor $script:C.Head
    Write-Host ("  semantics-ar  DEMO KIT   v{0}+g{1}{2}" -f $KitVersion, $ProductCommit, $dirtyTag) -ForegroundColor $script:C.Head
    Write-Host '  >>> TEST MODE - NOT production-signed. Throwaway demo VM only. <<<' -ForegroundColor $script:C.Stop
    Write-Host $bar -ForegroundColor $script:C.Head
    if ($HostLine) { Write-Host $HostLine -ForegroundColor $script:C.Faint }
}

function Write-Phase {
    param([Parameter(Mandatory)][string]$Title)
    $line = ('{0} ' -f $Title.ToUpperInvariant())
    $pad = [Math]::Max(0, 64 - $line.Length)
    Write-Host ''
    Write-Host ($line + ('-' * $pad)) -ForegroundColor $script:C.Head
}

# Render a single preflight/verify row. Status is one of GO/FIX/STOP/INFO.
function Write-Check {
    param(
        [Parameter(Mandatory)][ValidateSet('GO', 'FIX', 'STOP', 'INFO')][string]$Status,
        [Parameter(Mandatory)][string]$Name,
        [string]$Detail = ''
    )
    $color = switch ($Status) {
        'GO'   { $script:C.Ok }
        'FIX'  { $script:C.Fix }
        'STOP' { $script:C.Stop }
        default { $script:C.Info }
    }
    $tag = '[{0,-4}]' -f $Status
    Write-Host (' {0}  {1}' -f $tag, $Name) -ForegroundColor $color -NoNewline
    if ($Detail) { Write-Host ("   {0}" -f $Detail) -ForegroundColor $script:C.Faint } else { Write-Host '' }
}

function Write-SarLog {
    param(
        [Parameter(Mandatory)][string]$Message,
        [ValidateSet('info', 'ok', 'warn', 'err')][string]$Level = 'info'
    )
    $color = switch ($Level) {
        'ok'   { $script:C.Ok }
        'warn' { $script:C.Warn }
        'err'  { $script:C.Stop }
        default { $script:C.Info }
    }
    Write-Host ('  {0}' -f $Message) -ForegroundColor $color
}

function Write-Step {
    # "  action ....... result" alignment used by the install/configure phases.
    param([Parameter(Mandatory)][string]$Action, [string]$Result = 'OK', [ValidateSet('ok','warn','err')][string]$Level = 'ok')
    $dots = [Math]::Max(3, 50 - $Action.Length)
    $color = switch ($Level) { 'warn' { $script:C.Warn } 'err' { $script:C.Stop } default { $script:C.Ok } }
    Write-Host ('  {0} {1} ' -f $Action, ('.' * $dots)) -NoNewline -ForegroundColor $script:C.Info
    Write-Host $Result -ForegroundColor $color
}

# Yes/No that is safe under automation: when the host is non-interactive or -DefaultYes
# is chosen, it returns the default without blocking (post-reboot resume must never hang).
function Confirm-YesNo {
    param([Parameter(Mandatory)][string]$Prompt, [bool]$DefaultYes = $true, [switch]$Assume)
    if ($Assume) { return $DefaultYes }
    $suffix = if ($DefaultYes) { '[Y/n]' } else { '[y/N]' }
    try {
        $ans = Read-Host ("{0} {1}" -f $Prompt, $suffix)
    } catch {
        return $DefaultYes
    }
    if ([string]::IsNullOrWhiteSpace($ans)) { return $DefaultYes }
    return ($ans.Trim().ToLowerInvariant() -in @('y', 'yes'))
}

function Write-ReadyScreen {
    param([string]$Mode = 'ENFORCE', [string]$SandboxPath = 'C:\SarDemo\Sandbox')
    $bar = '=' * 64
    Write-Host ''
    Write-Host $bar -ForegroundColor $script:C.Ok
    Write-Host '                        DEMO READY' -ForegroundColor $script:C.Ok
    Write-Host $bar -ForegroundColor $script:C.Ok
    Write-Host (' Product  : installed, minifilter attached, service RUNNING') -ForegroundColor $script:C.Info
    Write-Host (' Mode     : {0}' -f $Mode) -ForegroundColor $script:C.Info
    Write-Host (' App      : open (system tray + main window)') -ForegroundColor $script:C.Info
    Write-Host (' Sandbox  : {0}  (safe demo files)' -f $SandboxPath) -ForegroundColor $script:C.Info
    Write-Host ''
    Write-Host ' RUN THE ATTACK DEMO (separate, safe, sandboxed):' -ForegroundColor $script:C.Head
    Write-Host '     attack\Demo-Attack.cmd            (follow attack\RUNBOOK.md)' -ForegroundColor $script:C.Info
    Write-Host ''
    Write-Host ' RETURN THIS VM TO NORMAL when finished:' -ForegroundColor $script:C.Head
    Write-Host '     Reset-SarDemo.ps1' -ForegroundColor $script:C.Info
    Write-Host ''
    Write-Host ' >>> TEST MODE build - not for production. Demo VM only. <<<' -ForegroundColor $script:C.Stop
    Write-Host $bar -ForegroundColor $script:C.Ok
}

Export-ModuleMember -Function Write-SarBanner, Write-Phase, Write-Check, Write-SarLog, Write-Step, Confirm-YesNo, Write-ReadyScreen
