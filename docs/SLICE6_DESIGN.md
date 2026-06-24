# Slice 6 — Keystore Self-Protection (Unit 3, Core) — Design Notes

This records the rationale the code cannot express. It is durable truth, not a restatement
of the source. It is subordinate to `CONSTITUTION.md`; where they disagree, the Constitution
governs. It builds on `SLICE1_DESIGN.md`..`SLICE5_DESIGN.md`.

## Scope delivered in this slice

Unit 3 makes the captured-key keystore — the system's sole recovery asset (II.5) — survive a
reboot, which it previously did not: the in-kernel store worked but its MAC key was generated
fresh on every load (`BCryptGenRandom`), so nothing on disk could ever be verified after a
restart, and recovery-after-reboot (required by VII.1.1) was impossible.

Delivered in the established two-tier shape:

- **`engine/` keystore-manager core (`sar_keystore_mgr.{h,c}`) — HOST-VERIFIED.** The load and
  persist *policy*: it drives the already-host-verified `sar_keystore_serialize` /
  `sar_keystore_verify`, and adds the two pieces those did not have — the **anchor/generation
  state machine** that distinguishes a benign crash window from a rollback/erasure, and the
  generation-bump + anchor emission on persist. Exercised by `tests/test_keystore_mgr.c`
  (15 checks, `/W4 /WX` clean): round-trip restore, crash-window accept, rollback reject,
  same-generation tamper reject, erasure detect, corrupt detect, wrong-key reject, overflow,
  fresh-start, anchor-establish.
- **`driver/keystore_persist.{h,c}` + integration — COMPILES + LINKS against the real WDK.** The
  kernel glue: a single `SAR_KEYSTORE` object that owns the non-paged record array, push lock,
  MAC key, generation, and anchor; boot-time load/restore; and a debounced background persist.
  It compiles clean with `cl /kernel` at `NTDDI_WIN11_GE` (`/W4`, only Microsoft-header macro
  noise — every `driver/` and `engine/` TU including `keystore_mgr.c` builds), and the full set
  **links into `semantics_ar.sys` (~102 KB), `link` exit 0**. The build-out fixed one real
  omission — `engine/src/keystore_mgr.c` was missing from `driver/CMakeLists.txt`, which would
  have left `sar_ksm_*` unresolved at link — and re-confirmed the §6 object-name caveat
  (`driver/capture.c` and `capture/src/capture.c` both yield `capture.obj`; the core is given a
  distinct `/Fo`). What is proven is **compile + link**, not behavior: the runtime validation
  below still requires a VM.

## The constitutional correction (VII.1.1 / VII.1.2 / VII.2.1)

External validation against primary Microsoft/TCG sources established three load-bearing facts
that the original Part VII text contradicted, so Part VII was rewritten to be implementable
(the document carries no change history; it now reads as if always written this way):

1. **A Windows kernel driver has no TPM access.** TBS is user-mode (`Tbsip_Submit_Command` over
   `DeviceIoControl`); kernel CNG is BCrypt-only (no NCrypt/Platform-Crypto-Provider in kernel);
   the `_treedrv`/TrEE surface is an authoring framework for secure-world service drivers, not a
   consumer seal/unseal API. The original VII.1.2 — *"unsealed once at early boot by the
   boot-start driver"* — was therefore impossible. **A TPM-sealed MAC key must be unsealed by
   the PPL-protected user-mode service (TBS) and handed to the kernel**, so the key transits user
   mode for one bounded boot-time window; VII.1.1's *"never exists in user mode"* was relaxed to
   exactly that window, justified by Part VII's own degrade-and-record discipline (VII.5).
2. **A SYSTEM/admin attacker without kernel-code execution defeats file ACLs** (`SeBackupPrivilege`
   reads any file ignoring the DACL; `SeRestore`/`SeTakeOwnership`/`SeManageVolume` likewise). So
   the on-disk file's ACL is *not* a secrecy or integrity boundary against the in-scope adversary
   (R7). On-disk **confidentiality** is therefore exactly the strength of the platform seal: real
   where the records are encrypted under a sealed key, and otherwise honestly **descended and
   recorded** (the live in-kernel copy stays confidential by the CPU privilege boundary).
3. **HVCI is code integrity, not data confidentiality.** Kernel-pool secrecy from user mode is
   the CPU's user/kernel boundary, present with or without HVCI; HVCI's contribution is making it
   harder to *obtain* the kernel-code execution that would cross that boundary. VII.2.1 now says
   this precisely instead of attributing pool secrecy to HVCI.

The keystore's **integrity / tamper-evidence** (keyed-MAC chain + external anchor) is unchanged
and always-on; only the confidentiality and the seal *mechanism* moved.

## [DECISION] Core vs. Hardening split

The platform-rooted seal (TPM unseal via the service, application-PCR cap, TPM-NV anchor, or a
VBS-enclave key holder) requires a user-mode service component, a new IOCTL, PPL coupling, and
VM validation, and on the stated self-protection floor (Win10 22H2 / Server 2022) VBS enclaves
do not exist (24H2 / Server 2025 only). That is a separate, VM-bound unit. **Unit 3-Core**
(this slice) delivers cross-reboot survival on *every* platform with the honest baseline:

- the MAC key is generated in kernel and persisted in an OS-ACL-protected key file
  (`mackey.bin`), which on a bare machine a SYSTEM attacker can read — recorded, not hidden;
- the keystore is serialized, MAC-chained, and persisted (`keystore.bin`); the external anchor
  `{generation, head_mac}` lives in `mackey.bin`, apart from the keystore file;
- on a TPM/VBS machine this same baseline runs until **Unit 3-Hardening** replaces the key
  acquisition with a sealed backend and adds on-disk AEAD encryption.

This is constitutional by VII.5: where the sealing primitive is absent the guarantee descends to
the kernel-line boundary and is recorded (`posture.keystore_persistent`,
`posture.keystore_tamper_detected`); the recovery mechanism itself never depended on the seal.

## [DECISION] The anchor / generation state machine (host-verified)

Rollback — an attacker swapping the current keystore file for an older internally-valid one — is
caught only by an anchor the file cannot itself roll back. `sar_ksm_load` verifies internal
integrity (reusing `sar_keystore_verify` with no anchor) and then decides against the trusted
anchor:

- `file.gen < anchor.gen` → **rollback**, reject;
- `file.gen == anchor.gen` and `head_mac != anchor.head_mac` → **tamper**, reject;
- `file.gen == anchor.gen` and head matches → steady state, accept;
- `file.gen > anchor.gen` → **crash window** (the file was durably written but the anchor update
  did not land before a crash), accept and advance the anchor;
- empty file with a live anchor (`gen > 0`) → **erasure**, reject.

The ordering that makes this sound is **write the keystore file durably (flush) before advancing
the anchor**: a crash then leaves the file at most one generation ahead of the anchor (the
accepted case), never the reverse. On any reject the kernel does not load the suspect file — it
records `keystore_tamper_detected`, starts the live store empty, and continues capturing at
`generation = anchor.generation` so the next persist supersedes the suspect file. Lost pre-reboot
keys there are a confirmed limit (VIII.2), not a new defense surface.

## [DECISION] Kernel persistence shape

- **Single owner.** `SAR_KEYSTORE` (in `keystore_persist.c`) owns the record array, push lock,
  MAC key, generation, and anchor. `capture.c` no longer owns the keystore; it calls
  `SarKeystoreAppend` / `SarKeystoreReady` / `SarKeystoreMacKey`. This removes the duplicated
  store the chassis-era capture context carried (operating contract 1.9).
- **Debounced background persist via a dedicated system thread**, not a DPC→work-item path: the
  thread waits on a stop event with the debounce timeout and persists when a `dirty` flag is set.
  This avoids the rundown/IRQL hazards of acquiring rundown protection in a DPC and gives a clean
  join at teardown (set stop → `KeWaitForSingleObject(thread)`), with a final flush inside the
  thread before it exits. A persist delayed or dropped under a key storm is absorbed by
  cumulative-N exactly as a dropped Oracle sample is (III.2.2) — the same key recurs.
- **Crash-consistent atomic write**: serialize to a pool buffer, write a `.tmp`, `ZwFlushBuffersFile`,
  then `FileRenameInformation`(`ReplaceIfExists`) over the live name. TxF is deprecated and is
  deliberately not used. Records `[0..c)` are immutable once written (append-only + dedup), so the
  persist serializes the first `c` under a shared lock without blocking on growth; if more arrived
  it re-marks dirty.
- **Boot load is deferred out of `DriverEntry`** (a boot-start minifilter cannot read the system
  volume that early) to an `IoRegisterBootDriverReinitialization` callback, which fires after
  devices are started. Captures before the load completes are gated off by `SarKeystoreReady`
  (counted as `preload_drops`, absorbed by cumulative-N), so no capture is ever recorded under a
  throwaway key.
- **Self-I/O is excluded from capture.** The persist thread's own writes to `keystore.bin`/`.tmp`
  re-enter `SarPreWrite`; `SarCaptureProvenanceIsOwn` skips any write whose normalized path
  contains the `SemanticsAr` directory segment, preventing both self-capture and an unsafe
  pre-image read of our own file mid-write. This skip is the minifilter-level analogue of the
  forward-direction self-recovery exemption (III.3.1) — our writes are never the attacker's.

## Rejected / deferred (with reasons)

- **On-disk AEAD encryption of the records:** deferred to Unit 3-Hardening. It is only meaningful
  with a sealed key (otherwise the encryption key shares the file's fate, B1); without a TPM/VBS
  seal the honest posture is descended confidentiality, which Core records. The MAC chain already
  gives integrity/tamper-evidence now.
- **Append-only on-disk log + compaction** (instead of full re-serialize per persist): a real
  optimization the chained MAC already supports, but it carries its own torn-write/compaction
  design and tests; full atomic-replace is correct and simplest now. Deferred as a performance
  follow-on, not a correctness gap.
- **TPM-NV monotonic-counter anchor:** the right rollback primitive in theory, but on Windows
  TBS frequently blocks third-party NV access and owner-auth is usually unavailable; the anchor
  lives in `mackey.bin` for Core and rises to NV/VBS only where the platform actually grants it
  (Unit 3-Hardening).

## What the host tests prove — and what they do NOT

`tests/test_keystore_mgr.c` proves the manager *policy*: every load disposition (OK / EMPTY /
ROLLBACK / CORRUPT / OVERFLOW / INVALID_ARG), the crash-window vs. rollback distinction, the
same-generation tamper check, and that restored records are byte-identical and the emitted anchor
matches. They do **not** exercise the Windows file I/O, the boot-reinit load timing, the
debounced thread, `KeStackAttachProcess`-free persistence, the SD/ACL, or the self-I/O skip —
all in the unverified `driver/keystore_persist.c`.

## Open items for the VM follow-on (do not guess)

Compile + link are done (above); everything below is behavioral and needs a VM with
test-signing / kernel debugging — a genuine environment constraint, not a deferred chore.

- Validate boot timing: that the system volume is readable at the boot-driver-reinitialization
  point on the target matrix, and that a demand-loaded (test) install also reaches a load (the
  reinit callback fires only for boot-start drivers — the test caveat).
- Validate atomic write under power-loss injection (tmp+flush+rename) on NTFS for multi-MB files.
- Confirm the self-I/O skip actually suppresses self-capture and that `FltReadFile` is never
  issued against our own file mid-write.
- Unit 3-Hardening: service-side TBS unseal + IOCTL key delivery + application-PCR cap; TPM-NV or
  VBS-enclave anchor; on-disk AEAD; and the posture fields that turn "descended" into "rooted"
  where the platform supports it.
