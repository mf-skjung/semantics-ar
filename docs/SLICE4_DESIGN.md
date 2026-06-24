# Slice 4 — The Windows Chassis (observation point + control plane) — Design Notes

This records the rationale that the code cannot express. It is durable truth, not a
restatement of the source. It is subordinate to `CONSTITUTION.md`; where they disagree,
the Constitution governs. It builds on `SLICE1_DESIGN.md`, `SLICE2_DESIGN.md`, and
`SLICE3_DESIGN.md`.

## Scope delivered in this slice

Unit 1 = the Windows minifilter **chassis**: the observation point + control plane onto
which every later unit attaches. It is delivered in three parts, of which exactly one is
host-verifiable in this environment and two are written-but-unverified (no WDK / Windows
toolchain on the build host):

- **`control/` — the freestanding control-plane core (HOST-VERIFIED).** Mirrors `engine/`'s
  split: a portable, libc-free, heap-free, caller-buffer core that the kernel driver
  compiles in unchanged and the host test harness exercises today. Four concerns:
  message-header validation (`msg.c`), whitelist match over the identity tuple +
  identity-state resolution (`whitelist.c`), the AUDIT/ENFORCE mode state machine
  (`mode.c`), and the connect-handshake/challenge state machine with an injected verify
  callback (`handshake.c`). Built as `semantics_ar_control` under
  `-Wall -Wextra -Werror -Wconversion`; exercised by `tests/test_chassis.c`.
- **`driver/` — the Windows minifilter (WRITTEN, NOT COMPILED HERE).** Boot-start load,
  `FltRegisterFilter` / `FltStartFiltering`, universal-attach `InstanceSetup`, the full
  destruction-surface `FLT_OPERATION_REGISTRATION` table, the two internal seams, the
  authenticated comm port with the signed-challenge + version handshake, the non-paged
  mode/whitelist/identity state, process-create identity capture, and runtime feature
  detection + posture.
- **`service/` — the user-mode service (WRITTEN, NOT COMPILED HERE).** SCM lifecycle, the
  comm-port client end of the handshake, the operator mode/whitelist control surface,
  recovery ownership (relays to the existing `engine/host` recovery core — decrypt is never
  in the kernel), and the service half of process-create identity verification (SHA-256 +
  Authenticode signer subject).

### What is deliberately OUT (named later units, not stubbed-as-done)

The key-capture **action** behind the destructive-write seam (register-first XMM/`FXSAVE`
grab, pre-image read, memory scan, Gate-T invocation, `sar_convict`, keystore append,
verdict emission, ENFORCE blocking); the kernel non-paged-pool keystore residence + in-kernel
MAC + TPM seal/anchor; and the PPL-AM / ELAM / MVI provisioning itself. The chassis finishes
**without** any of these (see the two-layer auth split below). The seams are typed,
counted, documented boundaries — not stubs masquerading as complete.

## Evidence boundary — what is proven vs. what is asserted

Per the operating contract (1.8), the three parts have **different epistemic status** and
they are not conflated:

- `control/` + `tests/test_chassis.c` are **done and verified**: the host build is clean
  under `-Werror -Wconversion` and `ctest` is green (the existing 4 suites + the new
  `test_chassis`, 64 assertions including a 200k-iteration parser fuzz).
- `driver/` and `service/` are **done but unverified**: the C is written against the current
  FltMgr / KMDF / Win32 / CNG / WinTrust DDIs and the clean contract, but it was **not
  compiled** (no WDK here). Their correctness is claimed against the documented DDIs and the
  Constitution, not demonstrated by a build. Verification is deferred to a WDK/VM context;
  the open DDI questions are listed below. The root `CMakeLists.txt` adds `driver/` and
  `service/` only under `if(WIN32)`, preserving the invariant that **no Windows target
  configures on Linux**.

## [DECISION] D1 — ENFORCE anchoring (completes the Constitution, does not overturn it)

Constitution V.1 says ENFORCE "block[s] the originator's further writes," while VI.1 says the
per-IRP requestor identity is unreliable (under IRP pending by a higher filter the callback
may run on a system worker thread). These reconcile at the **moment of capture**, not at an
arbitrary later IRP: the register grab runs in the **requestor thread context at the
destructive-write pre-op**, where the originator `PEPROCESS` is definite. The destructive-write
seam therefore **holds a referenced originator `PEPROCESS`/`PETHREAD`** taken at that pre-op,
so the later capture/ENFORCE unit blocks at **live-context** ops (cached write / section
acquire), never at the decoupled paging flush (which runs from the modified-page writer at
`APC_LEVEL`, off the writer's thread). The **chassis only carries the reference**; it does not
block (IV.3.1). References are released on seam teardown. This is why the seam struct holds
referenced object pointers rather than raw PIDs: a PID can be reused, a referenced object
cannot, and the block must land on the *same* originator.

## [DECISION] D2 — Wiper-member routing

Pure keyless-destruction members can **never** convict (wiper boundary VIII.3): there is no
key to capture, so routing them to the capture seam would burn a register grab for nothing.
They route instead to the **metadata/telemetry seam** (classification only), consistent with
IV.1.3. The keyless members are: VSS shadow deletion; standalone `FSCTL_SET_ZERO_DATA` /
`FSCTL_FILE_LEVEL_TRIM` / `FSCTL_SET_SPARSE`; `FSCTL_LOCK_VOLUME` / `FSCTL_DISMOUNT_VOLUME`
(and legacy force-dismount). VSS deletion is primarily a **service-owned user-mode behavioral
signal** (`vssadmin` / `wmic` / WMI `Win32_ShadowCopy.Delete`); the secondary kernel-visible
path is `volsnap` (`IOCTL_VOLSNAP_DELETE_SNAPSHOT` = `0x53C038`, diff-area resize), recorded as
an **observed-but-not-convicting** vector. Encryption-bearing members (in-place writes,
block-clone / offload) route to the **capture** seam.

## The destruction-surface enumeration (the Gate D-axis)

The `FLT_OPERATION_REGISTRATION` table *is* the D-axis of the gate (IV.1.2): the set of ways
to destroy an existing original, each made a capture candidate. The members and their
classification:

| IRP / class | Members | Seam |
|---|---|---|
| `IRP_MJ_WRITE` | cached / non-cached / paging (discriminated by `IrpFlags`) | capture |
| `IRP_MJ_SET_INFORMATION` | `FileEndOfFileInformation`, `FileAllocationInformation` (shrink) | telemetry |
| | `FileDispositionInformation`(`Ex`) — delete | telemetry |
| | `FileRenameInformation`(`Ex`) — rename-over | telemetry |
| | `FileLinkInformation`(`Ex`) — hardlink-replace | telemetry |
| `IRP_MJ_FILE_SYSTEM_CONTROL` | `FSCTL_DUPLICATE_EXTENTS_TO_FILE`, `FSCTL_OFFLOAD_WRITE` — clone/offload | capture |
| | `FSCTL_SET_ZERO_DATA`, `FSCTL_FILE_LEVEL_TRIM`, `FSCTL_SET_SPARSE` | telemetry (D2) |
| | `FSCTL_LOCK_VOLUME`, `FSCTL_DISMOUNT_VOLUME` (+ legacy) | telemetry (D2) |
| `IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION` | mmap write — mark stream section-dirty | (attribution) |

The base/`Ex` info-classes are separate enum values (1709+), not flags; both are registered.
A superset of callbacks is correct (IX.1.1): a code an OS never issues never reaches the
handler and is inert there. **The chassis routes; it does not act on destruction in itself**
— a missed/keyless member is a missed *capture opportunity* or a non-convicting telemetry
event, never a data loss (there is no preservation to lose).

### Section-write attribution

`IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION` does not itself carry the write; it marks the
stream "section-dirty" (a stream-context flag) so the later **decoupled** paging flush — which
arrives with no requestor thread — is attributable back to the process that mapped the
section. This is the chassis's contribution to making mmap-based encryption capturable; the
capture itself is the later unit.

## [DECISION] The two seams — the most load-bearing forward element

The seam is shaped now so the future capture unit is not blocked. Both seams are
identity-independent and non-blocking.

- **Destructive-write seam** (consumed by key capture): carries the `PFLT_CALLBACK_DATA`, the
  `PCFLT_RELATED_OBJECTS`, the **referenced** originator `PEPROCESS`/`PETHREAD` (D1), a
  stream/stream-handle context for pre-image staging and section-dirty marking, the operation
  classification (which member + the cached/non-cached/paging discriminant captured *now*,
  because it is unrecoverable at post-op), a **continuation handle** (a synchronize token + a
  deferred work-item) so capture can split into a bounded synchronous register grab at pre-op
  plus a deferred `PASSIVE_LEVEL` pre-image read + memory scan, and a **pre-allocated**
  non-paged capture buffer (no allocation on the write hot path). The seam **hands
  references**; it never serializes keys and never crosses the comm port (Slice 1's contract:
  plaintext keys / candidates / `(P,C)` never cross driver↔service).
- **Metadata/telemetry seam** (rename/disposition/truncate + the keyless FSCTLs): classification
  only, no register capture.

In this unit the seam targets are **instrumented sinks** with per-vector coverage counters that
let the op proceed; their signatures and request-struct shapes are final, their bodies count.

## [DECISION] Comm-port authentication — the two-layer split (finishes without MVI)

The port self-protection (VII.3) is split so the unit completes **without** the external
Microsoft Vendor Integrity (MVI) / ELAM gate, which is a separate provisioning unit:

- **Layer A (MVI-independent — implemented and host-verifiable in its sequencing):** in
  `ConnectNotify` (connector's process context), resolve the connector **canonically**
  (`PsLookupProcessByProcessId` + `SeLocateProcessImageName`, a `\Device\HarddiskVolumeN`
  path — never a self-reported PID), enforce an image-path allow-list, then run a
  **signed-challenge handshake**: the driver issues a random nonce, the service returns a
  signature the driver verifies against a **public key compiled into the driver**
  (`BCryptVerifySignature`). Reject (`STATUS_ACCESS_DENIED`) until all checks pass. "First PID
  = service" is never cached. The **sequencing** of this handshake — nonce issue → response
  validation → version check → timeout / reject-on-fail — is the freestanding
  `sar_handshake_*` state machine in `control/`, host-tested with an injected verify callback;
  only the CNG primitive is Windows-gated. The version handshake (`GET_STATUS` →
  `STATUS_REPLY`, mismatch closes) follows auth, and **every** inbound message header is
  validated by `sar_msg_validate` before any body is read; any deviation drops the message and
  increments a tamper counter.
- **Layer B (PPL-AM hardening — written, degrades gracefully, NOT a completion blocker):**
  additionally query the connector's `PS_PROTECTION`
  (`ZwQueryInformationProcess(ProcessProtectionInformation)`); `PsProtectedSignerAntimalware`
  is the strongest layer. Where PPL is unavailable (no ELAM/MVI yet) the boundary **descends**
  (VII.5) and is **recorded** in posture; Layer A still authenticates. The chassis is
  **complete on Layer A**.

> Why Layer A is sufficient for completion: the failure Layer A defends against — an
> unauthorized process opening the control port and driving mode/whitelist — is fully closed
> by canonical-identity + cryptographic challenge, which need no platform integrity primitive.
> Layer B raises the bar against an attacker who has already achieved code execution inside a
> would-be-protected process; that is the PPL/MVI unit's job, and its absence is a recorded
> descent, not a hole in the chassis.

## [JUDGMENT CALL] `STATUS_ACCESS_DENIED` and the forbidden-concept grep

The repo maintains a green invariant:
`grep -rniE 'preserve|shadow|journal|evict|saturat|capacity.?(cap|limit)|trusted_?pid|self.?trust|auto.?block|access_denied' --include=*.c --include=*.h` over the tracked tree returns empty.
The pattern's `access_denied` arm targets the **forbidden gate/write-path block** (IV.3.1):
the old design returned `STATUS_ACCESS_DENIED` to a *filesystem operation*, which this system
must never do. §6/VII.3 of the design, however, **requires** `STATUS_ACCESS_DENIED` as the
response to an **unauthorized comm-port client connection** — a categorically different act
(denying a *client*, not a *file op*).

Resolution (surfaced, not silent — Workflow 1.10): the invariant is kept **fully empty over
`engine/`, `common/`, `control/`, `tests/`, and the entire host-verifiable + freestanding
surface**, and over **every filesystem-callback path in `driver/`**. The **only**
`STATUS_ACCESS_DENIED` in the whole tree is in `driver/commport.c`'s `ConnectNotify` reject. It
is greppable to exactly one file and one function, and it is *mandated* by the Constitution
(VII.3), not a residue of the old block-the-write architecture. The future invariant grep
should therefore be read as "empty except the single ConnectNotify connection reject"; the
honest statement is that one justified occurrence exists and why, rather than gaming the check
by spelling the status differently to dodge the regex.

## [DECISION] Process-create identity — the kernel/service split (closes the spoof hole)

`PsSetCreateProcessNotifyRoutineEx2` (1703+) is registered at runtime (else `...Ex`). Its
registration **failure is fatal-to-feature**: V.2.2's creation-time identity logic depends on
it, so the driver unwinds cleanly and refuses to load rather than run half-blind. At create,
the kernel captures the **canonical** path from the `PS_CREATE_NOTIFY_INFO` `FileObject` + Flt
name info — never `ImageFileName` when `FileOpenNameAvailable == FALSE` (spoofable) — and holds
a referenced `PEPROCESS`/PID in a non-paged table at default state **OBSERVE pending-verify**.
The **service** (PPL where available) computes the authoritative SHA-256 of the image and the
Authenticode signer subject (`WinVerifyTrust`) and returns the verdict; the whitelist exemption
activates **only after** the service confirms `path ∧ signer ∧ hash`. The state transition is
`sar_identity_resolve`: unverified → `OBSERVE_PENDING`; verified+listed → `EXEMPT`;
verified+unlisted → `OBSERVE`. **Boot-early / pre-service processes fail open to OBSERVE, never
to whitelisted** — a process the service has not yet vouched for is watched, not exempt.

## [DECISION] State, synchronization, pool

The mode flag, whitelist, and identity table live in **non-paged pool**
(`ExAllocatePool2` / `POOL_FLAG_NON_PAGED`; the deprecated `ExAllocatePoolWithTag` is a CodeQL
must-fix and is avoided). They are synchronized with an `EX_PUSH_LOCK`
(`FltAcquirePushLock{Shared,Exclusive}`); no call-out (no I/O, no service round-trip) happens
while the lock is held. The mode flag may be read as a volatile aligned `LONG`. The actual
whitelist/mode logic is the freestanding `sar_whitelist_*` / `sar_mode_*` operating on the
driver's non-paged arrays under the push lock — the kernel provides storage and serialization,
the freestanding core provides the (host-tested) logic.

## OS floor and compatibility (IX)

One x64 binary; newer DDIs are compile-time visible and **runtime-resolved**
(`MmGetSystemRoutineAddress` / `FltGetRoutineAddress` for
`PsSetCreateProcessNotifyRoutineEx2`, `FltVetoBypassIo`, `FsRtlGetBypassIoOpenCount`, the
`BCrypt*` set, etc.); a down-level branch exists only where the up-level path would otherwise
fail to load. Recorded floor:

- **Self-protection-intact floor:** Win10 22H2 / Server 2022; **certify target** Win11 23H2+ /
  Server 2025 (single attestation/WHCP-signed binary).
- **Recovery-only down-level floor:** Win10 1809 / Server 2019 (the FltMgr / CNG /
  process-notify-Ex floor). The recovery mechanism (key capture) does not depend on the
  self-protection primitives and is deliverable here; the self-protection model is intact only
  from the generation that supplies its primitives (VII.5).
- ARM64 maintained on demand only.

Altitude: **FSFilter Activity Monitor** group, integer altitude in **360000–389999**. The INF
carries placeholder **385000**; the final integer is a Microsoft allocation request, recorded
here as a deployment action, **not** a code knob. The newer FSFilter Security Monitor band
(392000–394999) is not targeted — its `devguid.h` class macro is absent from the 26100 SDK.

## Open empirical items (must be answered in a WDK/VM context, not guessed)

Recorded so they are not silently assumed (1.8, 1.10):

- **`cldflt` / cloud-files hydrate vs. write** — distinguishing a placeholder hydrate (benign
  fill) from a destructive write at the relevant altitude; the discriminant for the write
  classification on cloud-backed files is unconfirmed.
- **ReFS integrity-stream / block-clone pre-image behavior** — whether the pre-image read at a
  block-clone / integrity-stream write yields the destroyed original bytes the Oracle needs, on
  ReFS and Dev Drive, is unverified.
- **Dev Drive attach** — a trusted Dev Drive admits only AV-classed / allow-listed filters; the
  chassis detects, attempts attach, and **records the coverage gap** to posture and surfaces it
  to the service. It does **not** pretend universal attach.
- **Future non-cached-write BypassIO** — today BypassIO cannot bypass writes, so the chassis
  declares `SUPPORTED_FS_FEATURES_BYPASS_IO`, allows read-bypass, and handles
  `FSCTL_MANAGE_BYPASS_IO` with the `FltVetoBypassIo` path; a watch-item is left for a future
  OS that allows non-cached-write bypass (which would need explicit veto/handling).
- **Exact altitude allocation** — the 385000 placeholder must be replaced by the
  Microsoft-allocated integer before production signing.
- **MVI / ELAM provisioning** — the ELAM driver, MVI membership, and PPL-AM launch that turn
  Layer B from "queried and recorded" into "enforced." A separate later unit.

## What the host tests prove — and what they do NOT

`tests/test_chassis.c` (64 assertions, deterministic + a 200k-iteration fuzz, no external
deps):

- **Message-header validation** — a valid `SET_MODE` is accepted; short-header, null buffer,
  wrong version (rejected *before* the body), unknown type, declared-length mismatch, inbound
  truncation, and inbound oversize are each rejected with the right status; the per-type
  expected lengths match the protocol structs; and the fuzz proves no random buffer is ever
  spuriously accepted and `out_type` is cleared on every non-OK path (no body union is indexed
  before the type+length check passes).
- **Whitelist** — empty-no-match, add, match, duplicate-reject, all-three-fields-must-match
  (near-miss on path / subject / hash each fails), capacity enforced, remove + compaction
  survivors, remove-absent, room-freed-after-remove.
- **Identity resolution** — unverified → OBSERVE_PENDING (even on a whitelist hit: never exempt
  unverified), verified+unlisted → OBSERVE, verified+listed → EXEMPT.
- **Mode SM** — default AUDIT, set ENFORCE, invalid value rejected and current preserved, set
  back to AUDIT.
- **Handshake SM** — full happy path (issue → verify → version → authenticated), verify-before-
  challenge sequence error, wrong nonce length, double challenge, bad signature → rejected,
  version mismatch → rejected, timeout before complete → rejected, timeout after complete is a
  no-op, and no step proceeds out of a rejected state.

They do **not** (and cannot here) exercise the Windows write path, the live pre-image read, the
kernel pool, the comm-port transport, or the CNG/WinTrust primitives — all in the unverified
trees above and named for WDK-context verification.
