# Guard.psm1 - safety surfaces: the persistent TEST MODE wallpaper and helpers that keep
# the honesty banner on every screen. The throwaway-machine gating itself lives in
# Preflight (VM / domain / -IAmSure); this module carries the visual disclaimer.

Add-Type -ErrorAction SilentlyContinue @"
using System;
using System.Runtime.InteropServices;
public static class SarWallpaper {
    [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Auto)]
    public static extern int SystemParametersInfo(int uAction, int uParam, string lpvParam, int fuWinIni);
}
"@

$script:SPI_SETDESKWALLPAPER = 20
$script:SPIF_UPDATE = 0x01 -bor 0x02

function Get-CurrentWallpaperPath {
    try {
        return (Get-ItemProperty -Path 'HKCU:\Control Panel\Desktop' -Name WallPaper -ErrorAction Stop).WallPaper
    } catch {
        return ''
    }
}

# Apply the generated TEST MODE banner as the desktop wallpaper. Returns the prior wallpaper
# path so the caller can persist it for teardown.
function Set-TestModeWallpaper {
    param([Parameter(Mandatory)][string]$ImagePath)
    $prior = Get-CurrentWallpaperPath
    if (Test-Path $ImagePath) {
        Set-ItemProperty -Path 'HKCU:\Control Panel\Desktop' -Name WallPaper -Value $ImagePath
        Set-ItemProperty -Path 'HKCU:\Control Panel\Desktop' -Name WallpaperStyle -Value '10'
        [SarWallpaper]::SystemParametersInfo($script:SPI_SETDESKWALLPAPER, 0, $ImagePath, $script:SPIF_UPDATE) | Out-Null
    }
    return $prior
}

function Restore-Wallpaper {
    param([string]$ImagePath = '')
    if ($ImagePath -and (Test-Path $ImagePath)) {
        Set-ItemProperty -Path 'HKCU:\Control Panel\Desktop' -Name WallPaper -Value $ImagePath
        [SarWallpaper]::SystemParametersInfo($script:SPI_SETDESKWALLPAPER, 0, $ImagePath, $script:SPIF_UPDATE) | Out-Null
    } else {
        # No prior recorded: clear to a plain background rather than leaving the TEST MODE banner.
        [SarWallpaper]::SystemParametersInfo($script:SPI_SETDESKWALLPAPER, 0, '', $script:SPIF_UPDATE) | Out-Null
    }
}

Export-ModuleMember -Function Get-CurrentWallpaperPath, Set-TestModeWallpaper, Restore-Wallpaper
