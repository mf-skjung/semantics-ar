# Handoff — semantics-ar Windows realization — Slice 4 / chassis session (2026-06-24)

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

**Done and host-verified (builds clean `-Werror`/`-Wconversion`, `ctest` green):**
- `engine/` — ciphers, conviction battery, keystore v2 (MAC chain + anchor compare), recovery
  transform, Gate-T. (Slices 1–3.)
- `control/` — the freestanding chassis control-plane core: message-header validation, whitelist
  match + identity-state resolution, AUDIT/ENFORCE mode SM, connect-handshake/challenge SM.
  `tests/test_chassis.c`, 64 assertions incl. a 200k-iteration parser fuzz. (Slice 4, this
  session.)
- Contract extended: `protocol.h` gained the signed-challenge handshake wire format
  (`CONNECT_CHALLENGE` / `CONNECT_RESPONSE`) and the `STATUS_REPLY` message type; the validator
  and tests cover all message types 1–10.

**Done but UNVERIFIED (written this session against the DDIs + contract; NOT compiled — no WDK
on the build host; root adds them only under `if(WIN32)`):**
- `driver/` — the minifilter chassis (registration table, two seams, comm port + handshake,
  non-paged state, process-create identity, feature/posture). See `SLICE4_DESIGN.md`.
- `service/` — the user-mode service chassis (SCM, comm client + handshake, mode/whitelist
  control, recovery relay, identity verification).

**Forbidden-concept grep** (`preserve|shadow|journal|evict|saturat|capacity-limit|trusted_pid|
self_trust|auto_block|access_denied`) is empty over the host-verifiable tree and every
filesystem-callback path; the **sole** `access_denied` is the mandated comm-port `ConnectNotify`
reject in `driver/commport.c` (rationale: `SLICE4_DESIGN.md`, "JUDGMENT CALL").

## 3. Decisions & rationale not derivable from code

Recorded in full in `SLICE4_DESIGN.md`: **D1** ENFORCE anchoring (seam holds a referenced
originator `PEPROCESS`/`PETHREAD` taken in the requestor thread context at pre-op, so a later
block lands on the right live-context op, never the decoupled paging flush); **D2** wiper-member
routing (keyless destruction → telemetry seam, never the capture seam); the **two-layer auth
split** (Layer A canonical-identity + signed challenge finishes the chassis without the
MVI/ELAM gate; Layer B PPL query is recorded-and-degraded); the **`STATUS_ACCESS_DENIED`
judgment call** vs the forbidden grep; and the **OS floor** (self-protection-intact Win10 22H2 /
Server 2022; recovery-only Win10 1809 / Server 2019).

## 4. Remaining work (ordered; each: boundary / definition-of-done / deps)

1. **Capture path (Unit 2) — the heart's kernel action.** *Boundary:* fills the
   destructive-write seam delivered in Slice 4 — register-first XMM/`FXSAVE` grab under a
   bounded synchronous pre-op hold, post-release `PASSIVE_LEVEL` pre-image read + memory-scan
   fallback, Gate-T (`sar_gate`) invocation on the `(P,C)` sample, the gate→battery request
   assembly (turning `sar_gate_result_t` + `(P,C)` + the destruction-member tag into a battery
   request and binding provenance for per-file keying), `sar_convict`, verdict emission over the
   comm port, and ENFORCE blocking using the D1-held referenced originator. *DoD:* every
   convicted host-replayed encryption yields a keystore append + a `VERDICT_NOTIFY`; ENFORCE
   blocks the originator's *next live-context write* and nothing else; the gate is never on the
   IRP wait path; host-replayable portions are tested. **Owns the capture-buffer
   pre-allocation:** the seam's `capture_buffer` descriptor is carried but left `NULL` by the
   chassis (which reads no pre-image); this unit adds the non-paged **lookaside list**
   (`ExInitializeNPagedLookasideList` at load, `ExDeleteNPagedLookasideList` at unload,
   allocate-from on the deferred path) that fills it — the idiomatic no-hot-path-allocation
   mechanism. *Deps:* Slice 4 seam (done), `engine` battery/gate (done).
2. **Keystore self-protection (Unit 3).** *Boundary:* kernel non-paged-pool residence of the
   authoritative keystore + whitelist, in-kernel keyed MAC (the MAC key never in user mode), and
   the TPM-sealed key + external anchor (the on-disk format and anchor *comparison* already exist
   from Slice 1). *DoD:* an attacker without kernel-code execution cannot read or silently erase
   captured keys; rollback is caught by the anchor; degrades + records posture where TPM/HVCI
   absent (VII.5). *Deps:* Unit 2 (it produces the keys to store).
3. **Windows recovery wiring (Unit 4).** *Boundary:* the comm-port `RECOVERY_REQUEST` →
   kernel-keystore key-material read → the existing `engine/host` recovery core →
   `RECOVERY_DONE`, plus volume enumeration, ReFS/same-volume specifics, large-file streaming,
   and residue clearing. The service relay end is the chassis (done); the **named typed boundary**
   where kernel key material feeds the recovery core is `sar_recovery_resolver_fn` /
   `sar_recovery_input_t` in `service/recovery.h` (the service installs a declining stub until the
   kernel keystore-read path is wired — it never fabricates keys). **Link prerequisite (engine
   change):** `semantics_ar_recovery_host` is declared only under `if(NOT WIN32)` in
   `engine/CMakeLists.txt` because `host/recover_file.c` uses POSIX `open`/`mkstemp`/`fsync`; the
   service links it on Windows, so this unit (or a small engine sub-task) must give
   `sar_recover_file` a Win32 atomic-write backing
   (`CreateFile`/`FlushFileBuffers`/`MoveFileEx(MOVEFILE_REPLACE_EXISTING)`) and let the target
   configure on `WIN32`. *DoD:* an operator recovery request recovers real files end-to-end on
   Windows; no-clobber holds. *Deps:* Units 2 and 3.
4. **PPL-AM / ELAM / MVI provisioning (Unit 5).** *Boundary:* the ELAM driver, MVI membership,
   and PPL-AM service launch that turn comm-port auth Layer B from "queried + recorded" into
   "enforced," and protect the service against injection (VII.4). *DoD:* the service runs PPL-AM
   where the platform supports it; absence still degrades to Layer A + recorded posture. *Deps:*
   none on the others; independently schedulable.
5. **Battery / recovery coverage closure (small, ongoing).** *Boundary:* the named gaps in the
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

## 6. WDK-context verification checklist (driver/service are written, NOT compiled)

Neither Windows tree was compiled here (no WDK on the build host). Before trusting them, compile
Release x64 against the WDK, run `InfVerif /h` + CodeQL Must-Fix, and confirm these specific DDI
points the sub-builders flagged as reconstructed-from-memory:

- **BypassIO registration:** whether `SUPPORTED_FS_FEATURES_BYPASS_IO` is the trailing
  `FLT_REGISTRATION` member at the targeted `FLT_REGISTRATION_VERSION` (vs. a separate call), and
  the exact `FltVetoBypassIo` signature, against the installed `fltKernel.h` (24H2).
- **VBS/HVCI detection structs:** `SYSTEM_ISOLATED_USER_MODE_INFORMATION` /
  `SystemIsolatedUserModeInformation` and `CODEINTEGRITY_OPTION_HVCI_KMCI_ENABLED` availability at
  the targeted `NTDDI_VERSION`.
- **TPM presence:** the TBS/`TpmExtractEK`-based proxy in `feature.c` is heuristic — replace with a
  real TBS-presence check if available.
- **Process-notify enum:** the `PSCREATEPROCESSNOTIFYTYPE` constant passed to
  `PsSetCreateProcessNotifyRoutineEx2`.
- **`ZwQueryInformationProcess(ProcessProtectionInformation)` → `PS_PROTECTION`:** confirm the info
  class + struct are exposed to kernel callers in the target headers.
- **Handshake signing primitive:** the service uses `NCryptOpenKey`/`NCryptSignHash` (persisted KSP
  key); reconcile the padding (`BCRYPT_PKCS1_PADDING_INFO`) with the driver's `BCryptVerifySignature`
  verify side so both agree on the scheme.
- **Comm-port reply framing:** reconcile the `FilterGetMessage`/`FilterSendMessage` /
  `FILTER_MESSAGE_HEADER` reply-buffer convention between `service/commclient.c` and
  `driver/commport.c` (whether our header is inline vs. behind the FltMgr reply header).
- **Port name** is single-valued (`L"\\SemanticsArPort"`) but duplicated in `driver/commport.h`
  and `service/commclient.h`; they currently match — keep them in sync (or hoist to `protocol.h`).
