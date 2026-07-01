# DEBUGGING.md — Getting a kernel dump out of the SarTarget VM

This is the runbook for debugging **driver-level** failures of `semantics_ar` inside the
Hyper-V test VM. It exists because we once burned a very long time on a kernel hard-hang
that left **no dump at all** — the dump subsystem was never configured to write one, so
every bugcheck/hang ended in a blind force-reboot. The fix is not exotic; it is two
standard-but-easy-to-miss configurations that must be in place *before* you trigger the
fault. Document them so we never pay that cost twice.

> **The one-line lesson.** A bugcheck (or an NMI-forced bugcheck) only produces a dump if
> the dump *destination* is already configured. On this VM the default pagefile-based path
> does not write, so you must point crash control at a **dedicated dump file** first. The
> two breakthroughs below — dedicated dump file, and NMI to convert a silent hang into a
> bugcheck — **compose**: neither alone is sufficient for a hang that drops no dump.

> **Where this fits.** TESTING.md governs *how and when* to verify (the cheap host tier vs. the
> slow VM tier, Part 6). This document is its slow-tier failure runbook: what to do when a
> driver test in the VM **crashes** — bugchecks or hard-hangs — and you need a kernel dump to
> diagnose it.

---

## 0. The environment (facts, not assumptions)

| Thing | Value |
| --- | --- |
| VM name | `SarTarget` (Hyper-V, **Gen2**) |
| Guest OS | Windows 11, build `26100` |
| Boot config | **Secure Boot OFF**, `bcdedit /set testsigning on` (required to load the unsigned test driver and to let KDNET rewrite boot settings) |
| Driver / filter | `semantics_ar` (minifilter); service `semantics_ar_service` |
| Load command | `fltmc load semantics_ar` |
| Host↔guest transport | **PowerShell Direct** (`New-PSSession -VMName SarTarget`), creds `admin`/`admin` — *lab-only throwaway VM, not a secret* |
| Networking for KDNET | `Default Switch`; host `192.168.224.1`, guest `192.168.224.x` |
| `kd.exe` | `C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\kd.exe` |
| Driver PDB | `<repo>\build_driver` (must be on the symbol path) |
| Base snapshot | `armed-kdnet` — a checkpoint with testsigning + dump config + KDNET already applied; restore it before a run instead of reconfiguring each time |

The driver is **DbgPrint-silent** by design; do not expect a `DbgView` trace. Live-state
observation outside the debugger is via `keystore.bin` / the preserve store, not console
output.

---

## 1. Symptom → action decision tree

- **"It bugchecks but I get no `MEMORY.DMP`."**
  The dump destination is not configured. → Do **§2 (dedicated dump file)**, reboot, reproduce.

- **"It just *hangs* — no bugcheck, VM frozen, PowerShell Direct stops responding."**
  There is no fault to dump yet. → Ensure §2 is done, then **§3 (force a bugcheck with NMI)**.
  This was our exact case: a synchronous `ZwQueryVirtualMemory` inside the write IRP
  hard-hung the kernel (FS/MM re-entrancy) and left no dump (see HANDOFF, "Memory capture
  must run OFF the IRP").

- **"I need to single-step / set breakpoints / inspect live, not just post-mortem."**
  → **§4 (KDNET live debugging)**.

- **"I have a dump; how do I read it against driver symbols?"**
  → **§5 (retrieve)** then **§6 (analyze)**.

---

## 2. Breakthrough #1 — Dedicated dump file (makes the dump actually write)

Configure crash control to write to a **dedicated dump file** on a fixed volume, bypassing
the pagefile-size/free-space constraints that silently suppress the default dump. All values
live under `HKLM\SYSTEM\CurrentControlSet\Control\CrashControl`. Run this in a guest
PowerShell Direct session, then reboot:

```powershell
$cc = 'HKLM:\SYSTEM\CurrentControlSet\Control\CrashControl'
Set-ItemProperty $cc CrashDumpEnabled   2            -Type DWord        # 2 = Kernel dump (see note)
Set-ItemProperty $cc DedicatedDumpFile  'C:\dedicateddump.sys' -Type ExpandString
Set-ItemProperty $cc DumpFileSize       3072         -Type DWord        # MB; manual size
Set-ItemProperty $cc IgnorePagefileSize 1            -Type DWord        # honor manual size, ignore pagefile
Set-ItemProperty $cc AutoReboot         1            -Type DWord        # auto-reboot so the host can reconnect & pull
Set-ItemProperty $cc Overwrite          1            -Type DWord
Remove-Item C:\Windows\MEMORY.DMP -Force -ErrorAction SilentlyContinue
# reboot to apply:
shutdown /r /t 3 /f
```

After reproduce, the dump lands at **`C:\Windows\MEMORY.DMP`** (minidumps, if any, at
`C:\Windows\Minidump\*.dmp`).

> **`CrashDumpEnabled` values (verified):** `1` = Complete, `2` = Kernel, `3` = Small
> (minidump, 64 KB), `7` = Automatic. We use `2` (kernel) for speed/size. **If you need to
> inspect pool (`!pool`) or user-mode memory of the offending process, use `1` (Complete)**
> — a kernel/minidump will answer `!pool` with *"pool page not in this dump."* (Equivalent
> shorthand: `wmic recoveros set DebugInfoType=2` == `CrashDumpEnabled=2`.)
>
> `AutoReboot=1` is a deliberate choice for the *automated* host-side pull loop (the VM
> reboots, the host reconnects over PowerShell Direct and copies the dump). For *manual*
> step-through capture you may prefer `AutoReboot=0` so the bugcheck screen stays up.

This is documented Microsoft behavior for "system drive can't hold the dump" — the
`DedicatedDumpFile` + `IgnorePagefileSize` + manual `DumpFileSize` triad is the supported way
to force a dump to a chosen volume.

---

## 3. Breakthrough #2 — NMI to turn a silent hang into a dump

A hard-hang produces no fault, so there is nothing to dump. From the **host**, inject a
non-maskable interrupt to force a bugcheck (`0x80 NMI_HARDWARE_FAILURE`); with §2 in place it
writes `MEMORY.DMP`, then (with `AutoReboot=1`) reboots:

```powershell
Debug-VM -Name SarTarget -InjectNonMaskableInterrupt -Force
Start-Sleep -Seconds 90          # let it bugcheck, write the dedicated dump, and reboot
```

**Prerequisites / caveats (verified):**
- **§2 must already be done.** NMI without a configured dump destination = another blind
  reboot (this is literally the trap we hit).
- On **Windows 8 / Server 2012 and later** (so: our Win11 guest), an NMI triggers a bugcheck
  **by default — no `NMICrashDump` registry value is needed.** Only pre-Win8 guests required
  `HKLM\...\CrashControl\NMICrashDump=1`. Note this so a future down-level VM doesn't confuse
  anyone.
- **Shielded VMs do not support NMI/debug.** `SarTarget` is not shielded, so this is fine.
- Equivalent manual prep per Microsoft/Dell guidance: `wmic recoveros set DebugInfoType=2`
  (= kernel dump) — already covered by §2.

---

## 4. KDNET live debugging (breakpoints, stepping, live state)

For interactive debugging rather than post-mortem. Gen2 + Secure Boot off is required so
`bcdedit` can write the debug transport settings.

**Guest (one-time, baked into the `armed-kdnet` snapshot):**
```cmd
bcdedit /dbgsettings net hostip:192.168.224.1 port:50000 key:1.2.3.4
bcdedit /debug {current} on
```
- **`port` must be ≥ 49152** (50000 is valid). **`key`** is four dot-separated alphanumeric
  groups (`1.2.3.4` is legal but weak — fine for a lab; use `kdnet.exe` to generate a strong
  one for anything shared).

**Host setup:**
```powershell
New-NetFirewallRule -DisplayName "SAR-KDNET" -Direction Inbound -Action Allow -Protocol UDP -LocalPort 50000
Connect-VMNetworkAdapter -VMName SarTarget -SwitchName "Default Switch"
```

**Attach from host** after the guest reboots into debug mode:
```cmd
kd -k net:port=50000,key=1.2.3.4
```

Reboot the guest into KDNET mode and the connection releases at boot; `kd` then has the
target. Use this *before* deep capture-path work — hard-hangs are otherwise invisible (HANDOFF
§5).

---

## 5. Retrieve the dump (host, over PowerShell Direct)

```powershell
$cred = New-Object System.Management.Automation.PSCredential('admin',(ConvertTo-SecureString 'admin' -AsPlainText -Force))
$sess = New-PSSession -VMName SarTarget -Credential $cred
Invoke-Command -Session $sess { Get-Item C:\Windows\MEMORY.DMP | Select Length,LastWriteTime }   # confirm it's fresh
Copy-Item -FromSession $sess -Path C:\Windows\MEMORY.DMP -Destination .\CRASH.DMP -Force
Remove-PSSession $sess
```

Always check `LastWriteTime` is newer than your repro — a stale dump from a previous run is
the classic false lead.

---

## 6. Analyze against driver symbols

The non-obvious bit is the **symbol path**: the public MS symbol server **plus the local
`build_driver`** directory that holds the `semantics_ar` PDB. Without the latter, the stack is
just addresses.

```cmd
set _SYM=srv*C:\symbols*https://msdl.microsoft.com/download/symbols;<repo>\build_driver
kd -z CRASH.DMP -y "%_SYM%" -c ".symfix; .reload; !analyze -v; .echo ===STACK===; kb; .echo ===SEMANTICS===; !stacks 2 semantics_ar; .echo ===LOCKS===; !locks; .echo ===RUNNING===; !running -ti; q"
```

High-value commands once loaded:
- `!analyze -v` — bugcheck triage, faulting instruction, suspected module.
- `kb` — stack of the faulting thread.
- `!stacks 2 semantics_ar` — every thread with a `semantics_ar` frame (find the offending IRP path).
- `!locks` / `!running -ti` — **deadlock/hang diagnosis** (which is what NMI dumps are for).
- `!pool <addr>` — pool block owner/tag (**needs a Complete dump, §2 note**).

---

## 7. Non-obvious traps (paid for in time)

1. **No dump ≠ no fault.** It usually means §2 was never applied on this VM. Configure the
   dedicated dump file first; only then is a bugcheck/NMI informative.
2. **Capture/memory-scan work must stay OFF the IRP.** Synchronous `ZwQueryVirtualMemory`
   inside the write pre-op hard-hangs the kernel via FS/MM re-entrancy — no dump, force-reboot.
   The deferred-worker design (Constitution II.3.2) exists *because* of this; don't reintroduce
   inline memory access on the write path.
3. **Restore `armed-kdnet` before each run** instead of re-applying testsigning/dump/KDNET — it
   is the known-good base and avoids config drift.
4. **Kernel dump can't read pool/user memory.** Switch `CrashDumpEnabled` to `1` (Complete)
   when `!pool` or the encryptor's user-mode heap matters.
5. **Confirm dump freshness** (`LastWriteTime`) before trusting an analysis.

---

## Sources (verified 2026-07)

- DedicatedDumpFile registry value — <https://learn.microsoft.com/en-us/archive/blogs/ntdebugging/how-to-use-the-dedicateddumpfile-registry-value-to-overcome-space-limitations-on-the-system-drive-when-capturing-a-system-memory-dump>
- Memory dump file options / `CrashDumpEnabled` values — <https://learn.microsoft.com/en-us/troubleshoot/windows-server/performance/memory-dump-file-options>
- Generate a kernel or complete crash dump — <https://learn.microsoft.com/en-us/troubleshoot/windows-client/performance/generate-a-kernel-or-complete-crash-dump>
- `Debug-VM` (Hyper-V) cmdlet — <https://learn.microsoft.com/en-us/powershell/module/hyper-v/debug-vm>
- Force manual memory dump on a Hyper-V VM (NMI) — <https://www.dell.com/support/kbdoc/en-us/000218315/force-manual-memory-dump-on-windows-hyper-v-vm>
- `BCDEdit /dbgsettings` — <https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/bcdedit--dbgsettings>
- Setting up network debugging of a VM with KDNET — <https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/setting-up-network-debugging-of-a-virtual-machine-host>
