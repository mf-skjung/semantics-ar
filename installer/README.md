# semantics-ar installer

A transactional installer for the full stack: the ELAM-signed minifilter driver, the
protected (PPL-AM) user service, the COM elevation host, and the WPF application.

## Build the payload (build machine)

Prerequisites already produced by the normal build:
- `build_driver\pkg\` — signed driver package (`scripts\package_driver.ps1`)
- `build_win\...\Release\` — `sarapi.dll`, `SemanticsArElevationHost.exe`,
  `SemanticsArElevation.tlb`, `semantics_ar_service.exe`, `sar_install.exe`

```powershell
.\installer\Build-SarPackage.ps1          # -> dist\  (app\, driver\, SemanticsAr-Setup.ps1)
```

`dist\` is self-contained and copyable to the target. The app is a **framework-dependent**
publish; the target needs the **.NET 10 Desktop Runtime**.

## Install / uninstall (target machine, elevated)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File SemanticsAr-Setup.ps1 -Action Install
powershell -NoProfile -ExecutionPolicy Bypass -File SemanticsAr-Setup.ps1 -Action Verify
powershell -NoProfile -ExecutionPolicy Bypass -File SemanticsAr-Setup.ps1 -Action Uninstall
```

- `-NoProtect` installs without ELAM/PPL launch protection (development installs; avoids the
  reboot-to-uninstall friction of a protected service).
- Logs: `%ProgramData%\semantics-ar\setup-<action>.log`. Install is transactional (rolls back
  on failure) and idempotent.

## What the operator must supply for a production install

1. **A production-signed driver** (MS attestation/WHQL). The bundled package is test-signed;
   the installer trusts its self-signed certificate (Root + TrustedPublisher) **only** when it
   detects a test signature. Replace `driver\*.sys`/`.cat` with the production-signed set and
   the cert-trust step becomes a no-op.
2. **A reboot to finalize uninstall** of the PPL-AM service (a non-protected caller cannot stop
   it live). The installer stages the delete and reports `REBOOT REQUIRED`.
