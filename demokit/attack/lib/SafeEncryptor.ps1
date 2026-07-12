# SafeEncryptor.ps1 - the guardrailed in-place overwrite primitive for the demo attack.
# It reproduces the ransomware-shaped behaviour the product is built to catch (in-place,
# high-entropy overwrite of a file that already has content on disk) but is confined by
# layered guardrails so it is impossible to point at real data:
#   1) the sandbox root is a literal returned by a function - there is NO path parameter
#   2) a canary file must be present and valid
#   3) only files listed in the sandbox's own manifest are touched
#   4) every path is resolved and asserted to live under the sandbox
#   5) a denylist of system/user roots aborts even if somehow reached
#   6) no network, no persistence, bounded work
# Dot-source this file; do not run it directly.

function Get-SandboxRoot {
    # The one and only target. Returned as a literal so no caller input can redirect it.
    return 'C:\SarDemo\Sandbox'
}

$script:DenyRoots = @(
    $env:SystemRoot,
    (Join-Path $env:SystemDrive 'Windows'),
    (Join-Path $env:SystemDrive 'Program Files'),
    (Join-Path $env:SystemDrive 'Program Files (x86)'),
    (Join-Path $env:SystemDrive 'Users'),
    $env:USERPROFILE,
    $env:ProgramData
) | Where-Object { $_ } | ForEach-Object { $_.TrimEnd('\') }

function Test-DenyRoot {
    param([Parameter(Mandatory)][string]$FullPath)
    $p = $FullPath.TrimEnd('\')
    # Reject UNC / removable-style and any denylisted root prefix.
    if ($p -match '^\\\\') { return $true }
    foreach ($deny in $script:DenyRoots) {
        if ($p.StartsWith($deny, [StringComparison]::OrdinalIgnoreCase)) {
            # Allow only if it is strictly inside the sandbox (sandbox lives on the system drive).
            if (-not $p.StartsWith((Get-SandboxRoot), [StringComparison]::OrdinalIgnoreCase)) { return $true }
        }
    }
    # A bare drive root (e.g. C:\) is never a valid target.
    if ($p -match '^[A-Za-z]:$' -or $p -match '^[A-Za-z]:\\?$') { return $true }
    return $false
}

function Assert-SafeSandbox {
    $root = Get-SandboxRoot
    if (-not (Test-Path $root)) { throw "sandbox not found ($root) - run Seed-Sandbox.ps1 first" }
    $canary = Join-Path $root '.sar-sandbox'
    if (-not (Test-Path $canary)) { throw "no sandbox canary - refusing to run" }
    if ((Get-Content $canary -Raw) -notmatch 'SAR-DEMO-SANDBOX') { throw "invalid sandbox canary - refusing to run" }
}

function ConvertTo-DemoCiphertext {
    param([Parameter(Mandatory)][byte[]]$Bytes)
    # AES with a throwaway random key + random IV, prepended, so the output is genuinely
    # high-entropy (what the detector keys on). The key is discarded - this is a demo, the
    # product's job is to make recovery possible without it.
    $aes = [System.Security.Cryptography.Aes]::Create()
    try {
        $aes.KeySize = 256
        $aes.GenerateKey(); $aes.GenerateIV()
        $enc = $aes.CreateEncryptor()
        $cipher = $enc.TransformFinalBlock($Bytes, 0, $Bytes.Length)
        $out = New-Object byte[] ($aes.IV.Length + $cipher.Length)
        [Array]::Copy($aes.IV, 0, $out, 0, $aes.IV.Length)
        [Array]::Copy($cipher, 0, $out, $aes.IV.Length, $cipher.Length)
        return $out
    } finally {
        $aes.Dispose()
    }
}

# Returns @{ Attempted; Written } - Written is what actually hit disk (in ENFORCE most are blocked).
function Invoke-SafeEncrypt {
    Assert-SafeSandbox
    $root = Get-SandboxRoot

    $manifestPath = Join-Path $root 'sandbox-manifest.json'
    if (-not (Test-Path $manifestPath)) { throw "sandbox manifest missing - run Seed-Sandbox.ps1" }
    $manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json

    $attempted = 0; $written = 0
    foreach ($rel in $manifest.files) {
        $full = [IO.Path]::GetFullPath((Join-Path $root $rel))

        # Guardrail 4 + 5: must be strictly inside the sandbox and not a denylisted root.
        if (-not $full.StartsWith(($root.TrimEnd('\') + '\'), [StringComparison]::OrdinalIgnoreCase)) {
            throw "containment breach - path escaped the sandbox: $full"
        }
        if (Test-DenyRoot -FullPath $full) { throw "denylisted target - refusing: $full" }
        if (-not (Test-Path $full)) { continue }

        $attempted++
        try {
            $bytes = [IO.File]::ReadAllBytes($full)      # a pre-image exists -> the product's gate
            $cipher = ConvertTo-DemoCiphertext -Bytes $bytes
            [IO.File]::WriteAllBytes($full, $cipher)     # in ENFORCE this is blocked at the first file
            $written++
        } catch {
            # A blocked write (access denied by the minifilter) lands here - that is the product working.
        }
    }

    # A ransom note, created ONLY inside the sandbox, for demo realism.
    $note = Join-Path $root 'READ_ME_TO_RESTORE.txt'
    Set-Content -Path $note -Value "Your demo files were encrypted by the DEMO attack script.`r`nThis is a SAFE, sandboxed simulation. Use the semantics-ar app to recover, or run attack\Reset-Attack.ps1." -Encoding ASCII

    return @{ Attempted = $attempted; Written = $written }
}
