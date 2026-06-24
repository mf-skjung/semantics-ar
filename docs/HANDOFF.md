# Handoff — semantics-ar Windows realization — Unit 6 service build + recovery-backend + contract reconciliation (2026-06-24)

This is the single living handoff artifact for the chain that builds the Windows product
against the clean Constitution. It supersedes and consolidates the scattered "Deferred (later
slices)" / "Named deferrals" sections of `SLICE1_DESIGN.md`, `SLICE2_DESIGN.md`, and
`SLICE3_DESIGN.md`. Read it after the Constitution and the four slice design docs. Each
implementer updates it; the terminal implementer deletes it.

> Do not restate what the code or the slice design docs already show. This captures only the
> chain state, the remaining units, and the traps.

## 1. Final goal (unchanged across the chain)

A single x64 Windows binary set — boot-start minifilter + user-mode service — that realizes
the Constitution: observe every destruction-of-an-existing-original, capture the encryption
key at the write (the Oracle), persist captured keys in a self-protected keystore, recover
files by decrypting their on-disk ciphertext, and respond under operator-chosen AUDIT/ENFORCE
with a user-owned creation-time whitelist. Host-verifiable logic stays freestanding and tested;
kernel-bound and Win32 code is verified in a WDK/VM context.

## 2. Current true state

**Done and host-verified (builds clean `-Werror`/`-Wconversion` on Linux gcc and `/W4 /WX` on
MSVC; `ctest` green):**
- `engine/` — ciphers, conviction battery, keystore v2 (MAC chain + anchor compare), recovery
  transform, Gate-T. (Slices 1–3.)
- `control/` — the freestanding chassis control-plane core: message-header validation, whitelist
  match + identity-state resolution, AUDIT/ENFORCE mode SM, connect-handshake/challenge SM.
  `tests/test_chassis.c`, 64 assertions incl. a 200k-iteration parser fuzz. (Slice 4.)
- `capture/` — the freestanding capture core (Unit 2): Gate-T → `sar_convict` orchestration and
  the verdict→keystore-record→`VERDICT_NOTIFY` projection with a tested zero-plaintext-key-leak
  invariant. `tests/test_capture.c`, 51 checks (ECB/CBC conviction, scan-buffer-primary path,
  gate skip, no-conviction, invalid inputs, provenance, key_id determinism, no-leak). See
  `SLICE5_DESIGN.md`.
- `engine/sar_keystore_mgr.{h,c}` — the keystore load/persist policy core (Unit 3-Core): the
  anchor/generation state machine (crash-window accept vs. rollback/erasure/tamper reject) over
  the existing serialize/verify. `tests/test_keystore_mgr.c`, 15 checks (round-trip restore,
  crash window, rollback, same-gen tamper, erasure, corrupt, wrong-key, overflow). See
  `SLICE6_DESIGN.md`.
- Contract: `protocol.h` signed-challenge handshake + `STATUS_REPLY`; validator/tests cover
  message types 1–10. The `VERDICT_NOTIFY` projection is now exercised by `test_capture`.

**Compiles + links against the real WDK, but NEVER LOADED/RUN (no runtime/VM validation yet):**
A WDK (26100 km headers + libs) is present on this host. The **entire `driver/` tree** — chassis
(Slice 4) + Unit 2 capture + the freestanding `engine` battery/`capture` core compiled into the
`.sys` (Slice 1: battery in kernel) — compiles clean with `cl /kernel` at `NTDDI_WIN11_GE` and
links into `semantics_ar.sys` (≈95 KB). The build-out fixed several real bugs (lookaside API,
`FLT_REGISTRATION`, missing `ntsystem.h` declarations, NTDDI, `_rotl` intrinsic — see §6). What is
proven is **compile + link**, not behavior: it has not been loaded, attached, or exercised.
- `driver/` chassis (Slice 4): registration table, seams, comm port + handshake, non-paged state,
  process-create identity, feature/posture.
- `driver/capture.{c,h}` (Unit 2, this session): two-phase kernel capture — **Phase 1** (pre-op,
  requestor context) copies `C`, reads pre-image `P` (`FltReadFile`), resolves provenance, snapshots
  the originator thread's own user stack (`PsGetCurrentThreadTeb` + `NT_TIB`, SEH-bounded 16 KiB),
  queues a generic work item, releases the IRP immediately; **Phase 2** (deferred `PASSIVE_LEVEL`
  worker) runs the freestanding core (gate → convict → project) → keystore append + `VERDICT_NOTIFY`
  send + ENFORCE originator registry. `EX_RUNDOWN_REF` drain + in-flight backpressure; lookaside
  work items. `operations.c` routes captures here and applies the ENFORCE block; `driver.c` owns the
  capture lifecycle. Memory-scan is primary; register-grab is a named follow-on (`SLICE5_DESIGN.md`).
- `driver/ntsystem.h` (this session): driver-declared semi-documented system structs/enums/prototypes
  (`PS_PROTECTION`, `SYSTEM_CODEINTEGRITY_INFORMATION`, `SYSTEM_ISOLATED_USER_MODE_INFORMATION`,
  `Zw*`) absent from the WDK km headers — **layouts need VM runtime validation** (§6).
**Done and host/compile-verified this session (Unit 6 — service build + recovery backend + driver↔service
contract reconciliation):**
- `service/` (user-mode exe) now **COMPILES + LINKS** clean (`/W4 /WX`, VS2022 x64, `semantics_ar_service.exe`
  ~73 KB) against the real Win32/CNG/WinTrust/fltlib SDK — the entire user-mode half had never been through a
  compiler before. Built via CMake (`build_win`), which configures `engine`+`control`+`capture`+`service`+`tests`
  on Windows (the `driver/` subdir self-skips when the WDK is not found by CMake; the driver is built by the
  standalone `cl /kernel` recipe in §6).
- `engine/host/recover_file.c` gained a **Win32 atomic-write backend** (the HANDOFF §4.2 engine prerequisite);
  `semantics_ar_recovery_host` now configures and builds on Windows. `tests/test_recover` is now enabled on
  Windows and **passes** (`ctest` 7/7 green on Windows incl. the no-clobber writeback over real files) — this is
  the host-verified, end-to-end exercise of the Win32 `ReplaceFileW` recovery path.
- The driver was **re-verified** (`cl /kernel` `COMPILE_OK`/`LINK_OK`) after the signing-scheme reconciliation
  below.
- The recovery **resolver** (`sar_recovery_resolver_fn`) remains a **declining stub** by design — wiring it to
  the kernel keystore-read is Unit 4 (below). This is the only intentional open seam in `service/`; it never
  fabricates keys.

**Compiles + links against the real WDK (this session):** `driver/keystore_persist.{c,h}` (Unit 3-Core
kernel glue) and the edits to `driver/capture.c` / `driver/driver.c` / `driver/driver.h`, plus
`engine/src/keystore_mgr.c` compiled into the `.sys`. The `SAR_KEYSTORE` object owns the non-paged
record array, push lock, MAC key, generation, and anchor; boot-time load runs from an
`IoRegisterBootDriverReinitialization` callback; a dedicated system thread does debounced atomic
persists (tmp + `ZwFlushBuffersFile` + rename); captures are gated on `SarKeystoreReady` until the
load completes and self-writes to the keystore directory are skipped. Every `driver/`+`engine` TU
compiles clean (`cl /kernel` `NTDDI_WIN11_GE`, `/W4`, only Microsoft-header noise) and all link into
`semantics_ar.sys` (~102 KB, `link` exit 0). The build-out fixed one real omission —
`keystore_mgr.c` was missing from `driver/CMakeLists.txt` (would have left `sar_ksm_*` unresolved) —
and reconfirmed the §6 `capture.obj` name-collision caveat (the core gets a distinct `/Fo`). Behavior
is **unrun**: VM validation remains. See `SLICE6_DESIGN.md`.

**Forbidden-concept grep** (`preserve|shadow|journal|evict|saturat|capacity-limit|trusted_pid|
self_trust|auto_block|access_denied`) is empty over the host-verifiable + freestanding tree and
every filesystem-callback path **except two Constitution-mandated `access_denied` occurrences**:
the comm-port `ConnectNotify` reject in `driver/commport.c` (VII.3; `SLICE4_DESIGN.md` JUDGMENT
CALL) and the V.1 ENFORCE block in `driver/operations.c` `SarPreWrite` (`SLICE5_DESIGN.md`). Both
are downstream-of-verdict / client-reject acts, not the old block-the-write residue.

## 3. Decisions & rationale not derivable from code

Recorded in full in `SLICE4_DESIGN.md`: **D1** ENFORCE anchoring (seam holds a referenced
originator `PEPROCESS`/`PETHREAD` taken in the requestor thread context at pre-op, so a later
block lands on the right live-context op, never the decoupled paging flush); **D2** wiper-member
routing (keyless destruction → telemetry seam, never the capture seam); the **two-layer auth
split** (Layer A canonical-identity + signed challenge finishes the chassis without the
MVI/ELAM gate; Layer B PPL query is recorded-and-degraded); the **`STATUS_ACCESS_DENIED`
judgment call** vs the forbidden grep; and the **OS floor** (self-protection-intact Win10 22H2 /
Server 2022; recovery-only Win10 1809 / Server 2019).

Recorded in `SLICE5_DESIGN.md` (Unit 2 capture path): the **freestanding/kernel split** (orchestration
+ projection are host-tested; only the grab/scan/deferral are kernel); **memory-scan is primary,
register-grab deferred** (XMM is clobbered by the write IRP, the key schedule lives in memory); the
**Phase-1 stack snapshot** (vs a Phase-2 cross-thread TEB read, which is undeclared and wrong-context);
the **second mandated `access_denied`** (V.1 ENFORCE block); and the **rejected** options (entropy
pre-screen — IV.2.3; in-kernel process termination — scope/philosophy). The driver build-out's
cross-cutting fixes (FLT_REGISTRATION, `ntsystem.h`, NTDDI, `_rotl`) are in §6.

**Unit 6 decisions (external-validated against primary Microsoft sources; not derivable from code):**
- **Handshake = ECDSA-P256 over the raw 32-byte nonce, no hash, no padding.** The build-out found the
  handshake could *never* have authenticated as written: the driver verified RSA-PKCS1 over the *raw* nonce
  while the service signed PKCS-less (`NCryptSignHash`, `pPaddingInfo=NULL`) over `SHA-256(nonce)` — both a
  scheme mismatch *and* an input mismatch (the driver never hashed). Resolved to ECDSA-P256 because the service
  was already padding-less (the documented ECDSA shape) and ECDSA removes the padding agreement point entirely
  and shrinks the compiled-in public key. The 32-byte nonce is used directly as the signed value on both sides
  (a high-entropy single-use challenge needs no pre-hash). Driver verify side switched to
  `BCRYPT_ECDSA_P256_ALGORITHM` + `BCRYPT_ECCPUBLIC_BLOB` + NULL padding; the compiled-in key is an ECC-P256
  public-blob **placeholder** (provisioned at deployment, like the prior RSA placeholder).
- **Win32 recovery writeback** = write temp in the same dir → `FlushFileBuffers` → **`ReplaceFileW`** (NULL
  backup honours no-preservation; preserves the original's DACL/timestamps/object-id/ADS) with a
  **`SetFileInformationByHandle(FileRenameInfoEx, REPLACE_IF_EXISTS|POSIX_SEMANTICS)` fallback** when the target
  is held open (e.g. by the still-running ransomware) — POSIX-semantics rename wins that race. The POSIX
  `fsync(parent-dir)` step was **dropped on Windows** (no equivalent; a directory `FlushFileBuffers` is
  undefined and fails on SMB — rename durability rides NTFS journaling). `REPLACEFILE_WRITE_THROUGH` is
  documented "not supported," so it is not used.
- **Identity verification hardening** (`service/identity.c`): hash + Authenticode trust are now bound to a
  **single locked handle** (`FILE_SHARE_READ` only → denies write/delete/rename for the eval window) to close
  the TOCTOU between hashing/verifying and the path-based signer lookup; revocation is **cache-only**
  (`WTD_CACHE_ONLY_URL_RETRIEVAL` + `WTD_REVOKE_WHOLECHAIN`) to avoid a network stall on the create hot path;
  the signer name is read via the explicit `CERT_NAME_ATTR_TYPE`+`szOID_COMMON_NAME` (deterministic, vs the
  `SIMPLE_DISPLAY` fallback chain). **Content hash stays the plain file SHA-256** (not the Authenticode PE
  hash): for a narrow, exact-match, revocable exemption a tighter "any byte change re-verifies" binding is
  preferable; re-sign stability is an anti-feature here. *Recorded hardening not taken (content-hash already
  neutralizes it, so it does not escape Constitution 0.3): pinning issuer + leaf SHA-256 thumbprint instead of
  the subject string — a future option if the identity tuple is ever widened.*
- **NCryptSignHash deadlock avoidance**: the service reports `SERVICE_RUNNING` **before** opening the signing
  key / connecting / running the handshake, because `NCryptSignHash` must not be called from the
  StartService/ServiceMain path while the SCM start-lock is held (primary-source Remark). The crypto now runs
  after the lock is released.
- **Operator control pipe DACL** (`service/control.c`) tightened from a **NULL DACL (every local process)** to
  SYSTEM + Administrators only (`D:P(A;;GA;;;SY)(A;;GA;;;BA)`) — the pipe drives mode/whitelist, so an open DACL
  was an authority hole.
- **Port name single-sourced** to `protocol.h` (`SAR_COMM_PORT_NAME`), removing the driver/service duplicates.
- Service receive path no longer mis-reads `FILTER_MESSAGE_HEADER.ReplyLength` as the inbound payload length
  (`ReplyLength` is the *expected reply* size, zero for fire-and-forget) — inbound length now comes from the
  app header via `sar_msg_validate`.

## 4. Remaining work (ordered; each: boundary / definition-of-done / deps)

> **Unit 2 (Capture path) is DONE this session** — freestanding core host-verified
> (`capture/`, `test_capture`), kernel glue compile+link-verified but unrun (`driver/capture.c`). DoD met for
> the host-replayable substance (convicted replays yield a keystore append + `VERDICT_NOTIFY`;
> the no-key-leak invariant is tested; the gate runs in the deferred phase, never on the IRP wait
> path; ENFORCE blocks the originator's next live-context write via the D1 referenced
> `PEPROCESS`). Memory-scan is primary; the XMM register-grab lane, mmap/section capture,
> clone/offload `(P,C)`, and a heap-walk scan are **named follow-ons** in `SLICE5_DESIGN.md`. The
> kernel glue now **compiles + links against the real WDK** (§6); only runtime/VM behavioral
> validation remains.

1. **Keystore self-protection — Unit 3-Core: DONE (host core verified; kernel glue written,
   uncompiled).** Cross-reboot survival now exists on every platform: the keystore + MAC key +
   external anchor persist to disk, are verified and restored at boot, and rollback/erasure/tamper
   are caught by the anchor state machine. The constitutional premise was corrected: a Windows
   kernel driver has no TPM access, so VII.1.1/VII.1.2/VII.2.1 were rewritten (sealed-key unseal is
   the PPL service's job; on-disk confidentiality and MAC-key secrecy descend and are recorded
   where no TPM/VBS seal exists — VII.5). See `SLICE6_DESIGN.md`.
   *Remaining — Unit 3-Hardening (VM-bound, raises the recorded descent to hardware-rooted):*
   service-side TBS unseal of a TPM-sealed MAC key + IOCTL delivery to the kernel + application-PCR
   cap; a TPM-NV-counter or VBS-enclave anchor; on-disk AEAD encryption of the records; and the
   posture fields that report "rooted" vs "descended." *DoD:* on a TPM/VBS platform an attacker
   without kernel-code execution cannot read captured keys off disk and cannot forge the store; the
   bare-machine path keeps Core's recorded descent. *Deps:* Unit 3-Core (done); couples to Unit 5
   (PPL) for the boot-time unseal window. *Remaining for Unit 3-Core itself:* only the VM behavioral
   validations in `SLICE6_DESIGN.md` (boot-load timing, atomic-write power-loss, self-I/O skip) — it
   already compiles + links against the real WDK.
2. **Windows recovery wiring (Unit 4).** *Boundary:* the comm-port `RECOVERY_REQUEST` →
   kernel-keystore key-material read → the existing `engine/host` recovery core →
   `RECOVERY_DONE`, plus volume enumeration, ReFS/same-volume specifics, large-file streaming,
   and residue clearing. The service relay end is the chassis (done); the **named typed boundary**
   where kernel key material feeds the recovery core is `sar_recovery_resolver_fn` /
   `sar_recovery_input_t` in `service/recovery.h` (the service installs a declining stub until the
   kernel keystore-read path is wired — it never fabricates keys). **Engine prerequisite — DONE
   (Unit 6):** `sar_recover_file` now has a Win32 atomic-write backing (`ReplaceFileW` + POSIX-rename
   fallback) and `semantics_ar_recovery_host` builds on Windows, host-verified by `test_recover`.
   **Remaining for Unit 4:** a comm-port/IOCTL message carrying `key_id` → an in-kernel keystore-read
   that returns the key material (algorithm/mode/iv/params, never plaintext over the wire unless the
   Slice-1 contract is revisited — note the current `RECOVERY_REQUEST` carries only `key_id`, so the
   key material must reach the service's resolver by a defined path), the resolver replacing the
   stub, plus volume enumeration, ReFS/same-volume specifics, large-file **streaming** (the Win32
   backend currently reads the whole file into memory, adequate for the host test, a named streaming
   follow-on for multi-GB), and residue clearing. *DoD:* an operator recovery request recovers real
   files end-to-end on Windows; no-clobber holds. *Deps:* Units 2 (done), 3 (Core done), and the
   Unit 6 service build (done).
3. **PPL-AM / ELAM / MVI provisioning (Unit 5).** *Boundary:* the ELAM driver, MVI membership,
   and PPL-AM service launch that turn comm-port auth Layer B from "queried + recorded" into
   "enforced," and protect the service against injection (VII.4). *DoD:* the service runs PPL-AM
   where the platform supports it; absence still degrades to Layer A + recorded posture. *Deps:*
   none on the others; independently schedulable.
4. **Battery / recovery coverage closure (small, ongoing).** *Boundary:* the named gaps in the
   slice docs — Salsa20/XSalsa20 lack an external KAT (self-consistency only); AES-192 register-
   granularity inversion is best-effort; HC-128/SOSEMANUK are confirmed-limit demotions. *DoD:*
   each gap either closed with an independent vector or re-affirmed as a recorded confirmed
   limit. *Deps:* none.

## 5. Traps & non-obvious context

- **Never `FLT_PREOP_SYNCHRONIZE` an async write** — it deadlocks the modified-page writer. The
  paging flush runs at `APC_LEVEL` off the writer's thread; the live key is gone by then. This is
  the whole reason D1 holds the referenced originator at the *first* (cached) write.
- **Per-IRP requestor identity is unreliable** (worker-thread context under higher-filter IRP
  pending). Identity is used in exactly one place: the creation-time whitelist. Do not reach for
  the requestor PID anywhere in capture routing.
- **`ImageFileName` is spoofable** when `FileOpenNameAvailable == FALSE`; always take the canonical
  path from the `FileObject` + Flt name info.
- **Plaintext keys / candidates / `(P,C)` never cross the comm port** (Slice 1 contract). The seam
  hands references inside the kernel; the port carries only `key_id` + algorithm/mode/provenance.
- **The keystore is the sole recovery asset** — there is no preservation anywhere. A missed write,
  a dropped Oracle-input sample, a keyless wiper member: each is a confirmed limit absorbed by
  cumulative-N, never a data loss. Do not "fix" any of them by reintroducing a backup.
- **Altitude 385000 is a placeholder** — replace with the Microsoft-allocated integer before
  production signing.
- **Comm-port SD is at default ACL.** `SarBuildPortSecurity` uses
  `FltBuildDefaultSecurityDescriptor(FLT_PORT_ALL_ACCESS)`; the spec's "tighten so only the
  service SID has `FLT_PORT_CONNECT`" is deferred because the service SID is not fixed at this
  unit's scope. Runtime identity is still enforced in `ConnectNotify` (canonical image
  allow-list + signed challenge). Add the SID-restricted ACL once the service SID is known
  (install/PPL unit).
- **Open empirical items** (answer in WDK/VM, do not guess): `cldflt` hydrate-vs-write
  discrimination, ReFS integrity-stream / block-clone pre-image behavior, future non-cached-write
  BypassIO. Listed in `SLICE4_DESIGN.md`.

## 6. WDK-context verification checklist (driver compiles+links but is UNRUN; service still uncompiled)

> **Update (this session): the driver now COMPILES AND LINKS against the real WDK.** A WDK is
> present (`Windows Kits\10` 26100 km headers + libs + VS2022). All **26 object files** — every
> `driver/` TU + the borrowed `control/` + `engine/` battery/gate/keystore/sha256 + all 7 ciphers
> + the freestanding `capture/` core — compile clean with `cl /kernel` against the real
> `fltKernel.h`/`ntifs.h` at `NTDDI_WIN11_GE` (24H2; only `/W4` noise inside Microsoft's own
> headers) and **link into `semantics_ar.sys` (≈95 KB), `link` exit 0**. Bugs found and fixed by
> this build-out:
> - **(Unit 2)** mixed lookaside API (`NPAGED_LOOKASIDE_LIST` + `…Ex` calls → uniform
>   `LOOKASIDE_LIST_EX`); undeclared `PsGetThreadTeb` (Phase-2 cross-thread TEB read redesigned to
>   a Phase-1 `PsGetCurrentThreadTeb` stack snapshot in the originator's own context — better and
>   DDI-correct).
> - **(Unit 1 / Slice 4, were blocking the whole driver)** `FLT_REGISTRATION` had 2 extra
>   initializers — it has exactly 16 members and **no `SUPPORTED_FS_FEATURES_BYPASS_IO` field**
>   (that macro is an `ntifs.h` feature bit, not a registration member); removing it both compiles
>   and satisfies the Constitution IV.1.2 "do not declare `SUPPORTED_FS_FEATURES_BYPASS_IO`" (the
>   FSCTL-handler mediation in `bypassio.c` is unchanged). `PS_PROTECTION` / the
>   `SYSTEM_CODEINTEGRITY_INFORMATION` / `SYSTEM_ISOLATED_USER_MODE_INFORMATION` structs + their
>   info-class values + `ZwQuery{System,InformationProcess}` prototypes are **not in the WDK km
>   headers** (answering the §6 questions below): added as a new `driver/ntsystem.h` from verified
>   public definitions (`SystemCodeIntegrityInformation`=0x67, `…IsolatedUserMode…`=0xA5,
>   `CODEINTEGRITY_OPTION_HVCI_KMCI_ENABLED`=0x400, `PsProtectedSignerAntimalware`=3, the 16-byte
>   IUM struct). `NTDDI_VERSION` corrected `NTDDI_WIN10_RS3`→`NTDDI_WIN11_GE` (RS3 violated IX.1
>   and hid `ExAllocatePool2`/`FSCTL_MANAGE_BYPASS_IO`, used since Slice 4).
> - **(engine)** `cipher_common.h` MSVC `_rotl/_rotr/_rotl64` needed `#pragma intrinsic` so they
>   inline instead of emitting CRT calls unresolved under `/NODEFAULTLIB`.
>
> **Reproducible build recipe (until a CMake-WDK toolchain or EWDK is wired):** `cl /kernel /GS-`
> with `INCLUDE = <kit>\km;<kit>\km\crt;<VC+ucrt+shared>`, defines
> `_AMD64_ AMD64 _WIN64 NTDDI_VERSION=0x0A000010 _WIN32_WINNT=0x0A00 POOL_NX_OPTIN=1`, then
> `link /DRIVER /SUBSYSTEM:NATIVE,10.00 /ENTRY:DriverEntry /NODEFAULTLIB` against
> `fltMgr.lib ntoskrnl.lib hal.lib ksecdd.lib` (km\x64).
>
> **Caveats that are still NOT proven by a clean compile+link (do not overclaim):** (a) the
> hand-declared `ntsystem.h` struct *layouts* (esp. the 0x10-byte IUM struct) must be validated at
> runtime in a VM — a wrong size silently disables HVCI/VBS detection (degrades safe per VII.5, but
> is not "working"); (b) `driver/capture.c` and `capture/src/capture.c` both yield `capture.obj` —
> a CMake `MODULE` build needs distinct object names (the direct build used distinct `/Fo`); (c) no
> INF validation, test-signing, load, or behavioral test was done — those need a VM. Still run
> `InfVerif /h` + CodeQL Must-Fix and confirm the points below in a VM:

- **Capture path (`driver/capture.c`, Unit 2) — the new high-risk surface:**
  - `FltReadFile` pre-image read at the write pre-op: confirm it does not stall or invert FS
    locks (paging/main resource) per filesystem and write-type; confirm
    `FLTFL_IO_OPERATION_DO_NOT_UPDATE_BYTE_OFFSET` semantics and that a cached read returns the
    pre-image (not the just-written bytes).
  - `FltAllocateGenericWorkItem`/`FltQueueGenericWorkItem(... DelayedWorkQueue ...)`/
    `FltFreeGenericWorkItem` signatures, and that the self-managed `EX_RUNDOWN_REF`
    (acquire-at-submit / release-in-worker / `ExWaitForRundownProtectionRelease` at destroy)
    composes correctly with `FltUnregisterFilter`'s own rundown wait — destroy ordering in
    `SarFilterUnload` is capture-before-comm-before-unregister.
  - Phase-1 stack snapshot: `PsGetCurrentThreadTeb` + `NT_TIB.StackLimit/StackBase`, SEH +
    `ProbeForRead`, bounded to `SAR_CAPTURE_SCAN_BYTES` (16 KiB) of the originator thread's own
    stack in requestor context. Validate at runtime that (i) the 16 KiB near `StackLimit` actually
    holds the just-used key schedule for representative ransomware, and (ii) the snapshot is safe
    when the pre-op is reached at `APC_LEVEL` (guarded: snapshot only at PASSIVE). Heap-walk scan
    is the named follow-on for schedules that live off-stack.
  - `FltSendMessage` with a relative timeout and `NULL` reply for the fire-and-forget
    `VERDICT_NOTIFY`; reconcile the header convention with `service/commclient.c`.
  - The engine battery + `capture` core compile **and link** into the `.sys` under
    `/NODEFAULTLIB` (proven this session — the `_rotl` CRT dependency was the only gap, fixed via
    `#pragma intrinsic`). `sar_gate_map_t` (8 KiB) and `sar_capture_result_t` are pool-allocated in
    `SarCaptureWorker`, never on the kernel stack.
  - **Register-grab lane (follow-on):** `KeSaveExtendedProcessorState` sized via
    `RtlGetEnabledExtendedFeatures`, IRQL `<= DISPATCH_LEVEL`.
- **BypassIO registration — ANSWERED + a surfaced Slice-4 defect (Unit 1, not Unit 2).** The
  26100 `FLT_REGISTRATION` has **exactly 16 members and no trailing `SupportedFeatures`/BypassIO
  field**, so `SUPPORTED_FS_FEATURES_BYPASS_IO` is **not** a `FLT_REGISTRATION` member — it must be
  a separate call. `driver/driver.c`'s `g_sar_registration` initializer therefore has one entry
  too many and fails to compile (`C2078: too many initializers`). **Fix (Unit 1):** drop the
  trailing `#if … SUPPORTED_FS_FEATURES_BYPASS_IO … #endif` initializer entry and register
  BypassIO support through its proper separate mechanism; reconcile the `FltVetoBypassIo`
  signature against `fltKernel.h` (26100). This blocks the full driver link until fixed.
- **VBS/HVCI detection structs — ANSWERED:** none of `SYSTEM_ISOLATED_USER_MODE_INFORMATION`,
  `SYSTEM_CODEINTEGRITY_INFORMATION`, their `SYSTEM_INFORMATION_CLASS` values, or
  `CODEINTEGRITY_OPTION_HVCI_KMCI_ENABLED` are in the 26100 km headers; declared in
  `driver/ntsystem.h` from verified public definitions. **Runtime-validate the IUM struct size
  (0x10) in a VM** — a mismatch silently fails VBS detection (`returned < sizeof` guard).
- **TPM presence:** the TBS/`TpmExtractEK`-based proxy in `feature.c` is heuristic — replace with a
  real TBS-presence check if available.
- **Process-notify enum:** the `PSCREATEPROCESSNOTIFYTYPE` constant passed to
  `PsSetCreateProcessNotifyRoutineEx2`.
- **`ZwQueryInformationProcess(ProcessProtectionInformation)` → `PS_PROTECTION` — ANSWERED:**
  `ProcessProtectionInformation` is in `ntddk.h`, but `PS_PROTECTION` / `PS_PROTECTED_SIGNER` /
  `ZwQueryInformationProcess` / `PROCESS_QUERY_LIMITED_INFORMATION` are **not** exposed to kernel
  callers in the 26100 headers; declared in `driver/ntsystem.h`. Runtime-validate that the PPL
  query succeeds and `PsProtectedSignerAntimalware` is read correctly in a VM.
- **Handshake signing primitive:** the service uses `NCryptOpenKey`/`NCryptSignHash` (persisted KSP
  key); reconcile the padding (`BCRYPT_PKCS1_PADDING_INFO`) with the driver's `BCryptVerifySignature`
  verify side so both agree on the scheme.
- **Comm-port reply framing:** reconcile the `FilterGetMessage`/`FilterSendMessage` /
  `FILTER_MESSAGE_HEADER` reply-buffer convention between `service/commclient.c` and
  `driver/commport.c` (whether our header is inline vs. behind the FltMgr reply header).
- **Port name** is now single-sourced in `common/.../protocol.h` (`SAR_COMM_PORT_NAME`); the
  driver/service duplicates were removed (Unit 6).
- **Service runtime validation (Unit 6 compiles+links + host-verified recovery core, but the live
  Win32 behavior is UNRUN — needs a VM with the driver loaded):** SCM start → `SERVICE_RUNNING`
  transition (with the deadlock-avoidance reorder); `FilterConnectCommunicationPort` to the live
  filter port; the ECDSA-P256 signed-challenge handshake against the driver over a real port (with a
  **real provisioned key pair** replacing the placeholder public-key blob in `driver/commport.c` and
  a matching KSP private key named `SemanticsArServiceKey`); the `FilterGetMessage`/`FilterSendMessage`
  framing against the live driver (Direction-A request = raw payload, Direction-B notify = app header
  after `FILTER_MESSAGE_HEADER`); the operator pipe with the restricted DACL; and end-to-end recovery
  on a real volume (the `ReplaceFileW`/`FileRenameInfoEx` writeback under power-loss and target-held-open
  conditions). PPL-AM launch (`ChangeServiceConfig2(SERVICE_CONFIG_LAUNCH_PROTECTED)`) compiles but is
  inert until the ELAM/signing unit (Unit 5).
