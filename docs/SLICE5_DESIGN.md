# Slice 5 — The Capture Path (Unit 2, the Oracle's kernel action) — Design Notes

This records the rationale that the code cannot express. It is durable truth, not a
restatement of the source. It is subordinate to `CONSTITUTION.md`; where they disagree,
the Constitution governs. It builds on `SLICE1_DESIGN.md`..`SLICE4_DESIGN.md`.

## Scope delivered in this slice

Unit 2 fills the destructive-write seam delivered as a counting sink in Slice 4 with the
actual key capture. It is delivered in two parts with **different epistemic status**, kept
separate exactly as Slice 4 keeps `control/` apart from `driver/`:

- **`capture/` — the freestanding capture core (HOST-VERIFIED).** A portable, libc-free,
  heap-free, caller-buffer module that performs the capture *orchestration* and *projection*:
  it runs Gate-T on the `(P,C)` sample, invokes `sar_convict`, and on a forward conviction
  projects the verdict into a keystore record (`sar_keystore_record_init`) and a
  `VERDICT_NOTIFY` wire message. Built as `semantics_ar_capture` under `/W4 /WX` (MSVC) /
  `-Wall -Wextra -Werror -Wconversion`; exercised by `tests/test_capture.c` (51 checks,
  all green, including the no-key-leak invariant).
- **`driver/capture.{c,h}` — the kernel glue (WRITTEN; COMPILES + LINKS against the real WDK).**
  The two-phase capture: a bounded synchronous pre-op grab (copy `C`, read pre-image `P`, snapshot
  the originator thread's own stack via `PsGetCurrentThreadTeb`) that releases the IRP immediately,
  and a deferred `PASSIVE_LEVEL` generic work item that calls the freestanding core, appends to the
  in-kernel keystore, emits the verdict, and (under ENFORCE) registers the originator for blocking.
  A WDK turned out to be installed: this and the entire `driver/` tree now compile clean with
  `cl /kernel` at `NTDDI_WIN11_GE` and **link into `semantics_ar.sys`** (see `HANDOFF.md` §6 for
  the recipe, the bugs the build-out fixed, and the runtime-validation caveats). What is *not* yet
  proven: behavioral correctness under a live load (the pre-image read's lock-safety, the stack
  snapshot actually holding the key, the hand-declared `ntsystem.h` struct sizes) — those need a VM.

## The decomposition decision — why the core is thin

The conviction battery (`sar_convict`, Slice 1) **already extracts candidates from both the
register-derived `candidates[16]` array and the raw `scan_buffer`** (it slides a 16-byte
window and scans for RC4 S-box state inside `scan_battery`). Building a separate candidate
extractor in the capture core would duplicate that and is forbidden (operating contract 1.9).
The core's genuine, non-duplicated responsibility is therefore only:

1. **Orchestration** — gate → (if fired) convict → classify the outcome
   (`SKIP_GATE` / `NO_CONVICTION` / `CONVICTED` / `INVALID`). This is the capture *policy*,
   absent from `engine/`.
2. **Projection** — turn a convicted `sar_verdict_t` into a keystore record and a
   `VERDICT_NOTIFY`, **with zero plaintext-key leakage into the notify** (Slice 1 contract:
   keys never cross the comm port). This is security-load-bearing and host-tested directly:
   `test_capture` scans the entire serialized notify for any 16- or 8-byte run of the key and
   asserts none is present.

The kernel supplies the captured material (register/scan blob, `P`, `C`); the core decides and
projects. Because the gate, battery, and keystore are already host-verified, the core's host
test replays a real engine-encrypted `(P,C)` plus the true key and proves the whole
gate→convict→record→notify pipeline end to end — the same discipline as `test_recover`'s A1
round-trips.

## Research-driven deltas applied (see HANDOFF §5 for the validation trail)

External validation (frontier WDK + ransomware threat intel) changed four design points from
the pre-research draft; each was independently re-verified against primary Microsoft sources
before adoption:

- **[DECISION] Scan-primary, register-grab deferred.** At the write IRP the AES-NI round-key
  schedule is reliably in **process memory** (the redundancy `aeskeyfind`/round-key inversion
  exploit), while the volatile XMM register file is almost always clobbered between the encrypt
  loop and the `NtWriteFile` boundary (x64 ABI: XMM0–5 caller-saved/scratch, XMM6–15 not
  carrying keys). The kernel glue therefore makes the **memory scan the load-bearing capture**
  (`candidate_count == 0`, `scan_buffer` filled) and the register-grab lane is a named
  follow-on. This is **not** a Constitution change: II.3.2 lists register-first *and*
  memory-scan, and II.3.1 anchors the key to the writing process's *memory*; "register-first"
  is permitted ("may"), not mandated.
- **[DECISION] Generic work item + rundown drain + backpressure.** `IoQueueWorkItem` pins a
  *device object* a minifilter does not own; the correct primitive is
  `FltAllocateGenericWorkItem`/`FltQueueGenericWorkItem`, whose rundown reference
  `FltUnregisterFilter` drains at unload (verified against Microsoft "Unregistering the
  Minifilter"). The glue adds its own `EX_RUNDOWN_REF` guarding the keystore/lookaside lifetime
  and a bounded in-flight cap (`SAR_CAPTURE_INFLIGHT_CAP`) that **drops to cumulative-N** under
  a write storm rather than queueing unboundedly — closing a self-inflicted DoS vector.
- **[DECISION] Clone/offload are coverage-only this slice.** `FSCTL_DUPLICATE_EXTENTS_TO_FILE`
  is a metadata-only VCN→LCN remap (verified against Microsoft "Block Cloning"): no data is
  read or written, so there is no `(P,C)` and no key at that operation, and a register grab
  there is pointless. The key (if the same process produced the ciphertext) lives in that
  process's memory, so clone/offload remain *scan/telemetry* candidates — but constructing
  their `(P,C)` is deferred. This slice captures only the in-place members
  (`WRITE_CACHED`, `WRITE_NONCACHED`) where `P` (pre-image read before release) and `C` (write
  buffer) are cleanly defined; paging writes (decoupled, key gone) and clone/offload are
  coverage-counted with named follow-ons. This **refines** the Slice 4 routing table
  ("capture seam" → "scan-only capture, no register grab"); it does not overturn IV.1.2.
- **[NOTE] Register-lane primitive.** When the follow-on register lane is built it must use
  `KeSaveExtendedProcessorState` (not raw `FXSAVE`) with the save area sized from
  `RtlGetEnabledExtendedFeatures` — the documented IRQL ceiling is `<= DISPATCH_LEVEL` (the
  `0x131` field failure is an *undersized buffer*, not an IRQL violation; the pre-research
  draft had both wrong and they were corrected here).

## [DECISION] The two-phase flow and what it honors

- **Phase 1 (pre-op, requestor context, bounded synchronous):** copy `C` (≤256 B from the
  write MDL/buffer, SEH-guarded), read `P` (`FltReadFile` at the write offset, ≤256 B, before
  the write destroys it — III.2.1), resolve the canonical provenance path
  (`FltGetFileNameInformation` normalized, never the spoofable `ImageFileName`), move the D1
  referenced originator into the work item, queue a generic work item, and let the IRP proceed.
  The gate is **not** run here.
- **Phase 2 (deferred, PASSIVE_LEVEL system worker, holding the D1 referenced originator):**
  `KeStackAttachProcess` to the originator, SEH-guarded bounded copy of the thread's user stack
  into a non-paged scan buffer, detach, then run the freestanding core (gate → convict →
  project). On conviction: append under the keystore push lock, increment
  `captured_key_count`, send `VERDICT_NOTIFY`, and under ENFORCE register the originator.

This honors: the gate is never on the IRP wait path (it runs in Phase 2); the battery is
offline; `P` is captured before destruction; async/paging writes are never
`FLT_PREOP_SYNCHRONIZE`d; plaintext keys/`(P,C)` never cross the comm port; and self-recovery
writes (plaintext over ciphertext) cannot convict — the forward-only direction does it with no
self-trust flag (III.3.1).

## [DECISION] ENFORCE blocking — and the second justified `access_denied`

On a forward conviction under ENFORCE, the convicted originator's **referenced `PEPROCESS`**
(not its PID — reuse) is added to a non-paged blocked registry. `SarPreWrite` checks the
current originator against that registry and completes its **next** live-context destructive
write with `STATUS_ACCESS_DENIED` (`FLT_PREOP_COMPLETE`). This is the Constitution-V.1-mandated
ENFORCE response, strictly downstream of a verdict — categorically different from the forbidden
gate/write-path block (IV.3.1).

> **Forbidden-concept grep update (surfaced, not gamed).** Slice 4 recorded that the *only*
> `access_denied` in the tree was the comm-port `ConnectNotify` reject. This slice adds a
> **second** justified occurrence: the ENFORCE block in `driver/operations.c` `SarPreWrite`.
> Both are Constitution-mandated (VII.3 client reject; V.1 ENFORCE block) and neither is a
> residue of the old block-the-write architecture. The honest invariant is now "empty over the
> host-verifiable + freestanding surface, and over every filesystem-callback path **except the
> single V.1 ENFORCE block**, plus the single VII.3 comm-port reject." The regex is not dodged
> by re-spelling the status.

## [DECISION] Unit 2 / Unit 3 boundary

Unit 2 owns the **append call, the record construction, the in-kernel non-paged store, its push
lock, and a MAC key seeded at load** (`BCryptGenRandom`, resolved dynamically). The store is
complete and append-correct (dedup by `key_id`, capacity `SAR_KEYSTORE_CAPACITY`). What Unit 3
adds is orthogonal and named: TPM-sealing the MAC key, the on-disk sealed copy, the external
anchor compare, and HVCI/pool-residence hardening. Until Unit 3, captured keys live in
non-paged pool and do not survive reboot — a **named deferral**, not a half-state: the append
API and record format are final.

## Rejected (critical analysis of the research, not blanket acceptance)

- **Entropy pre-screen on `C`** to avoid the unconditional pre-image read: **rejected.** IV.2.3
  forbids absolute entropy as the gate and IV.2.5 forbids pushing it into T; a `C`-only entropy
  screen reintroduces exactly that signal and the blind spot the conditional measure exists to
  avoid. The cached pre-image read is cheap; if it ever profiles as a real bottleneck the
  constitutional lever is the 256→128 block halving (IV.2.4), not entropy.
- **In-kernel process termination for a "durable" ENFORCE stop:** **rejected for Unit 2.** V.1's
  stance is that blocking is secondary and recovery is primary; kernel-driven termination is a
  service-side policy action (Unit 5 territory) and frictions with "the system never infers when
  to act." ENFORCE's per-originator leakiness is accepted by design.
- **"Per-file keys break cumulative-N":** **already handled, not a defect.** VIII.2 already
  records per-file-key non-capture as a confirmed limit absorbed by per-file cumulative-N (many
  chunk-writes per file). This is a Unit 4 recovery-accounting nuance, not a Unit 2 change.

## What the host tests prove — and what they do NOT

`tests/test_capture.c` (51 checks, deterministic, no external deps):
- **ECB conviction + projection** — a real AES-128-ECB `(P,C)` with the true key as a candidate
  convicts; the verdict, the keystore record, and the `VERDICT_NOTIFY` carry the right
  algorithm/mode/key_id; `notify.key_id == record.key_id`.
- **No-key-leak invariant** — the key appears nowhere in the serialized notify (16- and 8-byte
  scans).
- **CBC conviction + IV** — mode-agnostic capture; the block-0 IV is recovered into verdict and
  record.
- **Scan-buffer primary** — a key embedded in a raw `scan_buffer` (no candidate array) convicts,
  proving the scan-primary path end to end.
- **Gate skip** (identity `P==C`), **no conviction** (gate fires, no key present), **invalid
  inputs** (short sample, null `P`, null scratch).
- **Provenance propagation** and **key_id determinism/sensitivity** (same key → same id; one
  flipped byte → different id).

They do **not** exercise the Windows write path, the live pre-image read, `KeStackAttachProcess`
scanning, the lookaside/work-item lifecycle, the comm-port transport, or the keystore push lock —
all in the unverified `driver/capture.c` and listed for WDK-context verification (HANDOFF §6).

## Named follow-ons (each finishable, none host-verifiable here)

- **Register-grab lane** — `KeSaveExtendedProcessorState` (sized via
  `RtlGetEnabledExtendedFeatures`) at Phase 1, feeding the core's `candidates[16]` (zero core
  change; the API already accepts it).
- **mmap/section capture** — capture at `ACQUIRE_FOR_SECTION_SYNCHRONIZATION` (key still live)
  rather than the decoupled flush; the stream is already marked section-dirty in Slice 4.
- **Clone/offload `(P,C)` construction** — scan the cloning process at the FSCTL and pair its
  recent ciphertext.
- **Heap-walk scan** — the current scan copies only the originator thread's user stack; a VAD
  walk of committed private regions widens coverage.
- **cldflt placeholder skip** — detect dehydrated cloud placeholders and skip the pre-image read
  to avoid injecting hydration I/O onto the write path.

## Open empirical items (answer in WDK/VM, do not guess)

- Synchronous pre-image `FltReadFile` lock-safety per filesystem/write-type (NTFS/ReFS,
  cached/non-cached) — whether it can stall or invert FS locks at the write pre-op.
- `PsGetThreadTeb` + user-stack read while `KeStackAttachProcess`-attached — bounds validity and
  fault behavior under load.
- ReFS integrity-stream / Dev Drive pre-image readability; cldflt hydrate-vs-write discrimination.
- Confirm `FltQueueGenericWorkItem(... DelayedWorkQueue ...)` + `EX_RUNDOWN_REF` drain ordering
  against `FltUnregisterFilter`'s own rundown wait.
