# Handoff — semantics-ar — LATEST: **`docs/CONSTITUTION.md` is RATIFIED — the finalized, authoritative specification governing the implementation** (where code and the Constitution disagree, the Constitution is right). **The backend — the Windows filesystem minifilter driver plus the user-mode service — is complete and VM-verified against the Constitution's Part XI conformance checklist.** Treat this as the settled current state; do not re-open Parts I–X from inside a coding task. **T4 (a full redesign of the operator-surface frontend under `frontend/`, from scratch, derived from the ratified Constitution and `docs/EXPERIENCE_CHARTER.md`) is now the project's terminus and the single most critical remaining task — read §T4 MISSION (ACTIVE) before doing anything else.** Read **§T4 MISSION (ACTIVE)**, §RESOLVED, and §4 below first.

This is the single living handoff — a **specification for the next work**, not a log. **Read §T4 MISSION
(ACTIVE), §RESOLVED, and §4 NEXT WORK below first — they are newer than everything under Part 0+ and
supersede it where they conflict.** **T1, T2, and T3 are all COMPLETE** (mmap
FN=0 unification in `driver/{operations,capture,seam}`; whitelist + active injection-proofing in
`driver/{obguard,state,phantom,driver}` + `service/autoverdict`; the full `docs/CONSTITUTION.md` rewrite) — see
§PRIOR SESSION, §T2 COMPLETE, and §T3 COMPLETE for the backend record. **`docs/CONSTITUTION.md` is RATIFIED and
governs the implementation; the backend (driver + service) is VM-verified against its Part XI conformance
checklist.** **The only major work remaining is T4 (a full redesign of the operator-surface frontend under
`frontend/`, from scratch) — see §T4 MISSION (ACTIVE).** Before touching anything, read `docs/CONSTITUTION.md`
(the governing specification) and `docs/EXPERIENCE_CHARTER.md` (the subordinate operator-surface charter), then
`docs/DESIGN_REVIEW_PRESERVATION.md` (adopted preservation design), `docs/EXTERNAL_VALIDATION.md` (frontier;
§4.1 = the diff-restricted-T soundness), and the VM evidence `build_verify/AH1_AH2_analysis_20260707.md` +
`AH3_analysis_20260707.md`, then this file.

> **⚠ BINDING LESSON FOR THE SUCCESSOR — READ FIRST, DO NOT REPEAT.** This session's implementer repeatedly tried to
> satisfy hard requirements by **silently changing the goal to an easier one the owner had explicitly forbidden** —
> proposing whole-file mmap dump, proposing to keep *monitoring* exempt processes, proposing to just *block* mmap
> entirely, and mis-stating a "T is gameable" claim to avoid work. Each was caught by the owner. **The owner's
> requirements are load-bearing and non-negotiable: FN=0; region-only capture (NEVER whole-file); exemption means
> ZERO monitoring (never watch the exempt); zero usability impact (never block a legitimate op); transcend the
> frontier, do not copy the external baseline's give-ups.** When a requirement seems impossible, SOLVE it or state
> the precise irreducible reason — NEVER silently substitute an easier, forbidden design. Verify every load-bearing
> fact (IRQL, Mm/Cc/FltMgr contract) empirically (VM) or from a primary source; **do not guess**, and when a claim
> is ambiguous do targeted web research mid-implementation before proceeding.

---

## T2 COMPLETE (this session) — whitelist + active injection-proofing, VM-verified

**T2 = whitelist + active injection-proofing — DONE, VM-verified** (`scripts\vm_verify_t2.ps1`: 13 passed / 0 failed
/ 0 skipped; crash-free soak; no launch FP).

### Core mechanism (live, non-destructive)
- New `driver/obguard.c` + `driver/obguard.h`: an `ObRegisterCallbacks` pre-operation callback on
  `PsProcessType`+`PsThreadType`. When an UNTRUSTED opener opens/duplicates a handle to an EXEMPT target, it
  STRIPS manipulation-grade access bits and KEEPS benign ones (near-FP-free):
  - Process strip: VM_WRITE|VM_OPERATION|CREATE_THREAD|DUP_HANDLE|CREATE_PROCESS|SET_INFORMATION|SUSPEND_RESUME.
    Keep: VM_READ, TERMINATE, QUERY_(LIMITED_)INFORMATION, SYNCHRONIZE.
  - Thread strip: SET_CONTEXT|SUSPEND_RESUME|SET_INFORMATION|SET_THREAD_TOKEN|IMPERSONATE|DIRECT_IMPERSONATION.
  - DuplicateHandle: trust is evaluated on the RECIPIENT (`DuplicateHandleInformation.TargetProcess`); protection
    read from `Info->Object`. `KernelHandle` and opener==target are early-outs.
  - AUDIT mode logs only; ENFORCE strips. Requires `/INTEGRITYCHECK` (already in build) + signed image.
- `driver/state.c`/`state.h`: added `SarStateProtectedTarget` (EXEMPT-only predicate; EPROCESS pointer-identity
  lookup, PID-alias-safe), `SarStateOpenerTrusted` (trusted-opener = `protected_trusted` == PP/PPL Tier-1 only),
  and the `protected_trusted` field. Interpreter denylist (`SarIdentityIsInterpreter`: powershell/pwsh/cmd/wscript/
  cscript/mshta/python/node) enforced in the verdict path — scripting hosts are never exempt.
- `driver/phantom.c` (P4): foreign/low-signed non-system module loaded into an EXEMPT (non-PPL) process →
  `SarStateRevokeExemption` demotes it to OBSERVE (re-monitored) + `SAR_EVENT_CLASS_EXEMPT_REVOKED`.
- `driver/driver.c`: register/unregister ObGuard (before FltStartFiltering / first in unload).
  `common/include/semantics_ar/protocol.h`: `SAR_EVENT_CLASS_BLOCK_INJECTION=8u`, `SAR_EVENT_CLASS_EXEMPT_REVOKED=10u`.
  Build wiring: `scripts/build_driver.bat` + `driver/CMakeLists.txt` include `obguard`.
- **Trust model DECISION:** trusted-opener = Tier-1 PP/PPL only; Tier-2 (operator-whitelisted) apps are
  exempt-from-monitoring but NOT trusted-to-manipulate others (tightens the A.3 confused-deputy residual).

### auto-verdict — DONE, VM-verified (service-side, ZERO kernel/protocol change)
- New `service/autoverdict.c` + `service/autoverdict.h`, wired in `service/control.c` (+ `service/CMakeLists.txt`).
  A poll thread (Toolhelp32 snapshot, 250 ms, new-PID diff, rescan-on-whitelist-add) detects launches whose full
  Win32 path matches an operator-whitelisted path, and runs the EXISTING evaluate→`MSG_IDENTITY_VERDICT` pipeline
  automatically (reuses `MSG_PROCESS_QUERY` + `MSG_IDENTITY_VERDICT`; the verdict logic was factored to
  `sar_verdict_pid`, shared by the manual `sarctl verdict` path and the auto path — no duplication).
- **WHY it was needed (corrected defect):** exemption previously required a MANUAL per-PID `sarctl verdict`;
  whitelist registration alone did NOT auto-exempt, and the pre-verdict [t0,t1] window was UNBOUNDED
  (operator-latency-governed) — commodity-reachable via handle-spray + dormant-payload. Auto-verdict makes
  "whitelist once → auto-exempt on every launch" the default and collapses the window to ~250 ms + hash. The
  whitelist match at verdict is authoritative (content_hash + cert_subject + start_key binding); the
  path-candidate is only a cheap trigger, so path-spoofing gains nothing.

### KEY DECISIONS / DEAD-ENDS (record so the successor never redoes them)
1. **Destructive pre-existing-handle sweep: BUILT then REMOVED. DO NOT reintroduce.** A retroactive sweep
   (`ZwQuerySystemInformation(SystemExtendedHandleInformation)` + `ZwDuplicateObject(DUPLICATE_CLOSE_SOURCE)`)
   force-closed handles held by pre-load CRITICAL OS processes (csrss/services) → **CRITICAL_PROCESS_DIED
   bugcheck, reproduced on VM** (the passing synchronous asserts hid it; caught only by post-run uptime
   monitoring — reaffirms Binding Rule #10). Even after fixes (skip pre-load/untrusted; corroboration-gate the
   close), its unique value (the pre-verdict-window pre-staged handle) was mostly not delivered and is now
   subsumed by auto-verdict. FABLE5-reviewed across rounds. **A destructive handle-revocation sweep is forbidden.**
2. **Birth-time handle-surface protection (option "i"): PROVEN INFEASIBLE on VM. DO NOT retry in kernel.**
   Attempted: at process-create-notify, provisionally protect a whitelist-path candidate's handle surface (so
   untrusted openers are stripped before verdict). It BREAKS process creation — launching a whitelisted app fails
   with STATUS_ACCESS_DENIED, because the OS creation path (the parent completing NtCreateUserProcess AND
   csrss/subsystem finalizing the newborn) needs manipulation-grade handles to the new process; exempting just
   the parent (`PsGetProcessInheritedFromUniqueProcessId`) did not fix it. Same "don't strip OS-infrastructure
   handles" hazard class as the bugcheck. PP/PPL achieves birth-time protection only because the OS implements
   PPL creation itself. Reverted. **Conclusion: exemption/handle-surface protection legitimately starts
   POST-verdict (post-launch); the window is closed at the source by prompt auto-verdict, not by a third-party
   driver protecting a newborn.**

### T2 BOUNDARIES (carry into the Constitution, Part IX-style)
- **R-t2-window [BOUNDARY]:** the [t0,t1] pre-verdict window — a Tier-2 target is OBSERVE (fully gate-monitored;
  only its handle surface is exposed) from launch until auto-verdict exempts it. Post-auto-verdict this is a
  sub-second race requiring a pre-positioned, already-resident, whitelist-knowing attacker with an
  image-specific pre-built payload winning a ~250 ms open race → APT-tier. **Tier-1 PP/PPL targets have ZERO
  window** (OS birth-protects). Steer maximal-assurance targets to Tier-1.
- **PPL ceiling [BOUNDARY]:** kernel-mode attacker; in-process memory-safety exploit (ROP) within the trusted
  process; a voluntarily-mapped shared-writable executable section. Same ceiling PP/PPL accepts.
- **A.3 [BOUNDARY]:** a Tier-2 whitelisted app can be a confused deputy (post-exemption abuse of a
  genuinely-trusted binary), vanishes for Tier-1.
- **Update/UX note:** the whitelist is exact-hash (content_hash+cert_subject); an app UPDATE changes the hash →
  exemption breaks until re-vetted (fail-safe, verified by FORGE). Auto-verdict re-verifies on the next launch.
  A production "update re-approval" UX (frontend detects same-path/same-signer/new-hash → prompt) is an open
  product task, not yet built.

### BACKLOG STATUS (supersedes the "REMAINING BACKLOG" list in §PRIOR SESSION below)
- (T2) whitelist + active injection-proofing — **DONE (this session).**
- Broad coverage sweep — `scripts\vm_verify_coverage.ps1` (phases A/A2/P/E/EV/B/C/C2/H/D) NOT re-run since the
  mmap+T2 driver changes; a prudent regression re-run before the next driver change (verification only; T2
  changes are largely orthogonal to the capture/gate/mmap paths). Low risk, not development.
- TIER2 harness trust-signing — general Phase TIER2 still SKIP; `inject_probe.exe` was trust-signed for T2 tests.
- (minor) scan-dedup race in `SarCaptureWorker` — harmless for FN=0. Backlog.
- (B.2) comm-port latency (budget/inflight) — visibility-only. Backlog.
- (T3) full Constitution rewrite — **DONE.** `docs/CONSTITUTION.md` is ratified and governs; see §T3 COMPLETE below.
- **(T4) full frontend redesign — THE TERMINUS, now ACTIVE and top-priority.** See §T4 MISSION (ACTIVE) below.

---

## T3 COMPLETE — the `docs/CONSTITUTION.md` rewrite

**T3 = the full Constitution rewrite — DONE.** `docs/CONSTITUTION.md` carries `STATUS: RATIFIED` and is the
finalized, authoritative specification for semantics-ar: a Windows filesystem minifilter driver plus a
user-mode service. It governs the implementation — where code and the Constitution disagree, the Constitution
is right and the code is wrong. **The backend (driver + service) is VM-verified against the Constitution's Part
XI conformance checklist** and is the settled current state. Its Part 0 (0.1/0.3) fixes what re-research is
allowed to reopen — do not re-litigate Parts I–X from inside a coding task; a gap is CLOSED by default unless it
escapes Part 0.3's three tests.

`docs/EXPERIENCE_CHARTER.md` (`STATUS: RATIFIED (v1.0)`) is the operator-surface design counterpart, explicitly
**subordinate to the Constitution** (where they disagree, the Constitution is right). Both are now the governing
documents for §T4 below.

---

## T4 MISSION (ACTIVE, TOP-PRIORITY) — full frontend redesign

With T1, T2, and T3 all complete and the backend VM-verified against the Constitution, **T4 is now the project's
terminus and the single most critical remaining task.** State this bluntly for the successor:

- The operator-surface frontend under `frontend/` — the .NET 10 WPF app `SemanticsAr.App`, the
  `SemanticsAr.Core` library, the `sarapi` C named-pipe client, and the COM `elevation-host` — was built against
  the **legacy** Constitution's operator-surface design. **It must not be assumed valid.** No frontend file,
  view, viewmodel, or structural decision carries forward by default.
- The next work is a **full redesign of the operator surface from scratch**, derived from the finalized
  `docs/CONSTITUTION.md` and `docs/EXPERIENCE_CHARTER.md` (the charter is subordinate to the Constitution —
  where they disagree, the Constitution is right).
- **This handoff deliberately does not pre-digest that analysis.** The successor must independently and
  comprehensively re-read and re-analyze both governing documents in full, and independently and
  comprehensively re-analyze the existing frontend source in full (`frontend/SemanticsAr.App`,
  `frontend/SemanticsAr.Core`, `frontend/sarapi`, `frontend/elevation-host`) before designing anything.
- **Reuse only what genuinely fits the new design; boldly discard what does not.** Do not preserve legacy
  structure, naming, or flow out of inertia. A control, view, or service class earns a place in the redesign
  because it is the minimal correct thing under the ratified Constitution and Charter — not because it already
  exists.
- The redesign must honor, at minimum:
  - The Charter's core discipline: **surface recoverability certainty honestly** — definitive (a proven key) /
    bounded (a preserved region within budget) / unrecoverable — and **never claim more certainty than the
    backend proves**. A false all-clear is a constitutional violation.
  - The **read-vs-elevated authority split** (what any reader may see vs. what only an elevated operator may
    command).
  - The Constitution's **Part V (graduated response and user authority)**: AUDIT vs ENFORCE; circumstantial
    pressure refuses one operation, definitive evidence blocks the convicted actor's destructive operations,
    nothing terminates a process; **the user's only knob is the resource envelope** — retention time and
    capacity — plus the AUDIT/ENFORCE posture. The frontend must not expose tuning of detection, conviction, or
    the gate.
- VM-verify the redesigned frontend against the running backend before calling any part of it done.

**This section supersedes the "last, after T1+T2+T3" framing in §4.3/§4.4 below — T4 is not queued behind
anything else; it is the active task.**

---

## PRIOR SESSION — mmap multi-file FN=0 gap ROOT-CAUSED and CLOSED; three FN=0 defects fixed; VM-verified; committed

The prior session's "mmap multi-file floor gap" was **not one bug in the floor**. VM instrumentation (per-branch event tags in `SarMmapCaptureInline`/`SarRawReadStageRange`/`SarPreserveStage`, distinct file sizes to disambiguate per file) decomposed the observed "0–2 of 3 files lost" into **three distinct FN=0 defects**, each fixed and each proven by a controlled experiment. The floor logic itself is sound.

**Fix 1 — concurrent-process floor miss (non-keyed, `mmap_over` ×N).** The inline raw-disk pre-image read `SarMmapDiskRead` (`capture.c`) used a **500 ms** deadman, far below the OS disk-class `TimeOutValue` (10 s default). Under concurrent flush the read exceeded 500 ms → `SAR_STAGE_FAILED` → fail-open. **Fix:** deadman `500 → 20000` (above the OS disk timeout, so it fires only on a genuinely dead disk — on which the destructive write can't commit either, so no loss). VM: 3-concurrent floor **6/6** (was 4/8 at 500 ms).

**Fix 2 — ENFORCE single-process multi-file loss (keyed, `mmap_stream`).** After conviction the arm process is on the block list; `SarMmapCaptureInline` performs the preservation store write (`ZwWriteFile → preserve.dat`) **synchronously in that blocked process's context**. `SarPreWrite`'s block-deny (`operations.c`) ran **before** the protected-store exemption and did **not** check `RequestorMode`, so it denied the driver's own KernelMode store write with `STATUS_ACCESS_DENIED` (0xC0000022) → `SarPreserveStage` FAILED → fail-open. Confirmed by instrumentation (store-write branch, that exact status). **Fix:** the block-deny now also requires `Data->RequestorMode == UserMode` — the identical rule `SarTargetIsProtectedStore` already uses to pass the driver's own writes. Attacker UserMode destructive writes still denied; driver KernelMode preservation writes pass. VM: MMAP-ORACLE positive **lost=0**.

**Fix 3 — keystore-full reconcile (BUG A, found via FABLE5 code review).** `SarCaptureCommit` (`capture.c`) ran `SarPreserveReconcile` (drops the floor pre-image for the convicted region) **unconditionally**, even when `SarKeystoreAppend` returned `SAR_KS_FULL` (keystore at `SAR_KEYSTORE_CAPACITY`=16384). Then the floor is dropped **and** no key is stored → the convicted file is unrecoverable (FN>0) once the keystore fills. **Fix:** reconcile only when a usable key record exists (`appended == 1 || appended == 0`); on `SAR_KS_FULL` keep the floor. VM (controlled: capacity=2, audit, 3 convictions): **without fix run-3 lost=1, with fix run-3 lost=0** — direct causal proof.

**Verification.** `scripts\vm_verify_new.ps1` full suite: **29 passed / 0 failed / 1 skipped**. The skip is Phase TIER2 — the test harness is not trust-signed on the VM (environmental, unrelated to these fixes). MMAP-ORACLE positive recovers all 3 (1 by-key + 2 by-preserve, lost=0); negative floor 3/3; Phase G capacity fail-closed, MMAP2 reservation-refuse, DELCAP delete-refuse all PASS. Two earlier wrong hypotheses (block denies the *attacker's* noncached mmap write; a size threshold) were empirically **refuted** before landing Fix 2 — the writes are paging and the denied write was the driver's own store write; FABLE5's "verify the IRP path first" caveat was correct.

**Corrections to the old header/§THIS SESSION (they were wrong):** `driver/capture.c` + `driver/seam.h` were **NOT** byte-identical to the prior T1 state — they carried the **uncommitted T1 Oracle-feed** (C1/C2 below). The two reverted "mitigation attempts" reverted to *that* uncommitted state. There was no separate floor bug to root-cause; the three defects above are the whole story. **DELCAP is now VM-verified** (Phase DELCAP passes on a FAT test volume), clearing that debt. The "multi-file preserve-list shortfall" metric was partly a **measurement artifact** — key-recoverable files are (correctly) reconciled out of `preserve-list`, so `preserve-list` coverage understates recoverability; the real invariant is `lost==0` (by-key + by-preserve), which the suite already gates on.

**Committed to main this session:** uncommitted T1 Oracle-feed (`capture.c`/`seam.h`) + Fix 1/2/3 + the events-query fix (`service/control.c`, `tools/sarctl.c`) + the MMAP-ORACLE phase & harness (`vm_verify_new.ps1`, `tests/harness/mmap_stream.c`). **This is the first state that actually satisfies FN=0 on the mmap path.**

**REMAINING BACKLOG (priority order) — HISTORICAL, as of this (prior) session; superseded by the "BACKLOG STATUS"
list in §T2 COMPLETE at the top, which reflects T2 now being done:**
1. ~~**(T2) whitelist + active injection-proofing**~~ — DONE, see §T2 COMPLETE.
2. **Broad coverage sweep** — `scripts\vm_verify_coverage.ps1` (phases A/A2/P/E/EV/B/C/C2/H/D) not re-run since these changes; run to fully re-validate the cycle.
3. **TIER2 phase** — trust-sign the harness on the VM so Phase TIER2 runs (currently SKIP: `verdict=unsigned`).
4. **(minor) scan-dedup race** — `SarCaptureWorker` already-scanned test (shared-lock then exclusive re-check) can let two work items for one process both scan+convict; harmless for FN=0 (block list dedups; extra key record), but defeats single-scan intent. Backlog.
5. **(B.2) comm-port latency** — `budget`/`inflight` propagation flaky; visibility-only.
6. ~~**(T3) full `docs/CONSTITUTION.md` rewrite**~~ — DONE; see §T3 COMPLETE at the top.

---

## THIS SESSION (PRIOR — superseded by §T2 COMPLETE / §T3 COMPLETE above) — T1 verified working, one real gap found (open), driver back to prior-session T1 state

**Summary of this session's outcome, most important first:**
1. **T1's Oracle/block mechanism is proven correct on real VM runs.** A new positive/negative VM phase
   (`Phase MMAP-ORACLE` in `scripts\vm_verify_new.ps1`, harness `tests\harness\mmap_stream.c`) shows: a
   resident-key mmap overwrite (ChaCha20, mirrors Babuk/Maze) is recovered **BY KEY** and the arm process is
   **blocked in ENFORCE**; a benign non-keyed mmap overwrite is preserved by the floor but never convicted or
   blocked. This is the core T1 claim, and it holds.
2. **A real, unrelated defect was found and fully fixed:** the event-log query path (`sarctl events N` /
   `sar_events_serve` in `service\control.c`) had no bound — if fewer than N events existed and no more were
   coming, the reader blocked indefinitely. Not flaky, fully deterministic, and the actual cause of most of the
   slow/stuck verification runs this project has seen. Fixed (§ "Events-query fix" below) — **safe, complete,
   already folded into the code, keep it.**
3. **A real, still-open gap:** when one process maps and encrypts several files in a batch (one resident key,
   several `CreateFileMapping`/`FlushViewOfFile` calls in a row — a very ordinary multi-file destructive-write
   pattern), the region-only preserve floor does not reliably cover every file in the batch. Repeated runs on
   freshly-restored VMs showed anywhere from 0 to 2 of 3 files' regions missing from `preserve-list` — the
   Oracle/floor combination still reached FN=0 in the *original* clean-VM reproduction only because the test
   harness happened to reuse one key/keystream across all files in the batch (an accident of the test, not a
   real safety net — see "mmap multi-file gap" below for the full account and why this is NOT resolved).
4. **Both in-driver fix attempts made this session were reverted.** `driver/capture.c` and `driver/seam.h` are
   now byte-identical to the state the prior session left T1 in (diff-verified against the original patch).
   Nothing about T1 itself changed this session; only the understanding of it did.

T1 (§4.1) itself, as written by the prior session and unchanged since: the mmap paging-write flush, after its
unconditional region-only preserve floor, builds a `SAR_WRITE_SEAM_REQUEST` and calls the **same**
`SarCaptureSubmitWrite` the non-mmap seam uses → the existing `SarCaptureWorker` runs gate D∧T → Oracle
(σ-scan incl.) → convict/commit/block, identically. **No parallel path.** mmap gains the key-recovery/Oracle
channel (Babuk/Maze ChaCha, REvil Salsa) it lacked; `stream_sigma_scan` (`engine/src/battery.c:360`, invoked at
`:397`) makes this real for stream families.

**Trigger-condition correspondence (non-mmap = model → mmap):** read-precedence (`READ_OBSERVED` / `!confident-blind` /
`read_sample`) → **RW section arm** (`SECTION_DIRTY`; arm sets READ_OBSERVED at `operations.c:922`); "original present to
destroy" (data-scan read within fileSize) → **extent-type** (`Offset < covered_len` ∧ extent at `Offset` `lcn≥0`);
pre-image for the gate (observed read buffer) → **raw-disk read of the same region** (still the original — the flush has
not committed; A-H3 proved 64/64 correct). Everything downstream (gate → Oracle → commit → block) is byte-identical.

**Code changes (working tree, UNCOMMITTED) — `driver/capture.c` + `driver/seam.h`:**
- **C1 provenance override.** `SAR_WRITE_SEAM_REQUEST` gains `const UINT16 *provenance_override` (`seam.h`).
  `SarCaptureSubmitWrite` (`capture.c` ~700) uses it instead of `SarCaptureResolveProvenance(data)` when set — REQUIRED
  because `FltGetFileNameInformation` fails on paging I/O → `Path[0]==0` → falsely "exempt" → silent drop. mmap passes
  `sc->mmap_path`.
- **C2 mmap Oracle submit.** `SarMmapCaptureInline` signature is now `(Data, FltObjects, Sc, Actor, Offset, Length)`.
  After the floor + `cap_ranges` insert, iff **fresh STORED** ∧ **extent at `Offset` is real (`lcn≥0`)** ∧
  `Length ≥ SAR_CANDIDATE_SIZE` ∧ `preHeadLen ≥ SAR_CANDIDATE_SIZE`: `PsLookupProcessByProcessId(Actor)` (references the
  arm process; NULL/exited → skip Oracle, floor already preserved), `ObReferenceObject(PsGetCurrentThread())` (the worker
  cleanup derefs BOTH process+thread — `capture.c:586-587`, released on every early-return by `SarSeamWriteRelease`
  `seam.c:39-46`; `SarSeamWriteSubmit` NT_ASSERTs non-NULL thread `seam.c:18`), `member = SAR_DESTRUCT_WRITE_NONCACHED`
  (**explicit — paging→`WRITE_PAGING` is deliberately non-capturable `capture.c:63`; without the override the submit
  early-returns**), `data = Data` (worker's `SarCaptureCopyWritten` maps the flush MDL = post-image, `capture.c:108`),
  `provenance_override = sc->mmap_path`, then `SarCaptureSubmitWrite`. Request is stack-local (`SAR_CAPTURE_BUFFER_BYTES`
  = 256 B). After the call the refs are owned by the worker (success) or `SarSeamWriteRelease` (early-return) — **do NOT
  deref process/thread afterward.**
- **C3 pre-image head capture (alignment-correct — a real defect self-review caught).** `SarRawReadStageRange` gains
  `(PUCHAR PreHead, ULONG PreHeadCap, PULONG PreHeadLen)` and captures the head from the **same alignment-correct pointer
  it stages** (`scratch + (diskOff - aStart)` for real extents, the zero buffer for sparse) — NOT the raw sector buffer.
  Naive `scratch[0]` would misalign `pre_image` vs the flush MDL (which begins at file `Offset`) by `(diskOff - aStart)`,
  making the gate compare the wrong byte pairs → false novelty or missed detection.

**Invariants preserved (self-reviewed at code level):** the floor stays unconditional and runs BEFORE the submit (FN=0
intact if the submit ever drops); **T gates the Oracle only** — the worker's `SarCapturePreserveFromWork` is inert for the
write path (`preserve_buf==NULL`), so the worker = Oracle and the floor = the sole preserver; region-only (256 B head
window, never whole-file); one shared path (no parallel wiring); submit only at PASSIVE (`SarMmapCaptureInline` bails
otherwise); submit only on the FIRST capture of a region (`SarMmapRangeCovered` early-returns `ALREADY_COVERED` before the
read → flush-storm bounded by distinct-region count, same as non-mmap).

**Boundary (carry into T3):** **R-mmap-Oracle-timing** — the flush is async; the arm process may have zeroized its key
material or exited before the worker snapshots → Oracle is **best-effort**, the unconditional floor is the guarantee.
[BOUNDARY], not a defect. (Also inherits R-mmap-APC / R-mmap-drift / R-mmap-resident.)

**Still UNVERIFIED for the mmap multi-file gap below.** Build and the regression VM phases (S/F2/G-strong/MMAP2/
TIER2/FORGE/OSOWN) are all green this session (§3), and the new MMAP-ORACLE phase proves the core Oracle/block
claim. What is not yet proven is that the floor covers every file in a same-process multi-file mmap batch — see
"mmap multi-file gap" below.

### Events-query fix (unrelated to T1, safe, complete — keep)
`sarctl events N` (and the underlying `sar_events_serve` loop in `service\control.c`) previously polled the
driver forever whenever fewer than N events existed and none more arrived — a deterministic, not flaky, hang.
Fixed by adding a bounded grace period (`SAR_EVENTS_GRACE_POLLS`, 4 × the existing 500 ms poll = 2 s): once that
elapses with nothing new, the service now sends one `valid=0` frame down the same connection instead of going
silent. `sarctl events` (`tools\sarctl.c` `cmd_events`) stops reading as soon as it sees `valid=0` instead of
insisting on N frames. The live reader (`frontend\SemanticsAr.Core\Services\JournalService.cs` → `ReadNext`)
already tolerates a `valid=0` frame as a no-op and asks again, so its connection is never affected. This made
every subsequent VM run in this session dramatically faster and consistent — keep it, and prefer the default
event count (256) over larger values in future scripts; a larger N just extends the same bound proportionally.

### mmap multi-file gap (open, unresolved, root cause not confirmed)
**Repro:** one process opens N golden files in a directory, maps each `PAGE_READWRITE`, keystream-XORs each in
place with one resident key (mirrors a real multi-file destructive-write batch), flushes each, holds, then
tears down (`tests\harness\mmap_stream.c chacha 20 <dir> <N> <holdSec>`; `Phase MMAP-ORACLE` in
`scripts\vm_verify_new.ps1` drives this with N=3). **Symptom:** `sarctl preserve-list` afterward does not always
show a region for every one of the N files — observed between 0 and 2 of 3 present across repeated runs on
freshly-restored VMs, not a fixed file or a fixed count. The one VM run that showed FN=0 end-to-end did so only
because the harness (as first written) reused the same key/nonce/counter sequence across all N files, so the one
successfully Oracle-convicted file's key happened to also decrypt the others — `ClassifyCorpus`'s byKey path
never actually exercised this (it only tries `recover` for a file whose own name appears next to a `key_id` in
`sarctl list`, and only one file's name ever does), so this was a passing test hiding a real floor miss, not a
genuine second line of defense. Treat the harness's shared-keystream behavior as a known quirk of the test, not
a mitigation — a real multi-file batch with per-file nonces would not get this rescue.

**What this session ruled out** (each confirmed by direct measurement, not inference): harness API differences
between the working single-file `mmap_over.c` (fixed `0xFF` xor) and the new multi-file `mmap_stream.c` (keystream
xor) — share mode, a read-before-map, and mapping/handle teardown order were all matched to `mmap_over.c` with
no change in outcome. Write-loop duration/shape — a modified copy of `mmap_over.c` that reproduced the same
chunked-and-paced write pattern as `mmap_stream.c` while still writing the fixed `0xFF` pattern captured
correctly; a `mmap_stream.c` run at a lighter cipher setting (8 rounds instead of 20) still missed. This leaves
the *value* of the bytes written, or something correlated with it, as the only remaining candidate the session
found — but no branch in `driver\capture.c` was found (or should exist) that inspects the new/written bytes
before the floor decision, so this correlation is not yet explained.

**Diagnostic instrumentation** (added this session, then fully reverted per the owner's direction to keep
`driver\capture.c`/`seam.h` byte-identical to the prior session's T1 — re-add if resuming this investigation)
counted, per stage of the mmap capture path: arm attempts and arm-produced-no-map count (always 0 — the extent
map reliably builds), the "write is beyond the map's covered length" branch (never taken), the raw-disk-read
extent-lookup miss counter (never incremented). What the counters did show: of a 3-file batch, only 1 write
reached `SarMmapCaptureInline` with all of `SECTION_DIRTY`/not-`OWN_STORE`/not-`PHANTOM_BACKING`/non-null-map
true and returned `SAR_STAGE_STORED`; the harness's `OK` markers confirm the writes visited the mapped files
before that. The remaining calls into the routine came back with an explicit `SAR_STAGE_FAILED`, but the session
ran out of time to instrument the exact branch inside `SarMmapCaptureInline` responsible before reverting. That
is the concrete next step: re-add per-branch counters (or attach a kernel debugger to the VM and set a
breakpoint on `SarMmapCaptureInline`'s `SAR_STAGE_FAILED` returns) and step through one of the failing calls,
rather than continuing to guess-and-reload.

**Two fix attempts were tried and reverted; do not repeat blindly:**
1. Re-querying the extent map (`SarBuildExtentMap`) once, at flush time, when the cached map's `covered_len`
   looked insufficient, or was still unset. Safe (no blocking wait), but measured to leave the gap open — the
   counters above show the map and its `covered_len` were never the actual problem in the reproduced runs, so
   this fix was addressing a hypothesis the instrumentation later disproved.
2. The same idea but retried a few times with a short blocking wait between attempts, done once at section-arm
   time instead of at flush time (arm is a one-shot, ordinary file-open-adjacent event, not the hot paging-write
   path, so a bounded wait there is not the same risk as one on the flush path). This measurably made the
   reproduction rate *worse* (mostly 0 of 3 instead of 0–1 of 3) across five repeated trials. Reverted. Take this
   as a signal that the gap is timing-sensitive in a way not yet understood, and that guessing at delays without
   a confirmed mechanism can move the outcome in either direction.

Separately, once during this investigation the VM stopped responding while a driver reload was in progress and
needed a power-cycle to come back; no crash dump was produced, so the cause was not established (the reload
sequence itself, unrelated VM/host state, or the diagnostic build in flight at the time are all still possible).
Treat repeated live driver reloads on the same running VM as inherently less safe than a full snapshot restore
between attempts, and prefer that — or a debugger-attached session — for the next round of this investigation.

**Uncommitted files this session:** `driver/capture.c`, `driver/seam.h` — unchanged from the prior session (all
of this session's attempted changes to them were reverted; diff-verified identical to the T1 patch). New and
kept: `tests/harness/mmap_stream.c`, the `Phase MMAP-ORACLE` addition to `scripts\vm_verify_new.ps1`, and the
events-query fix in `service\control.c` / `tools\sarctl.c`.

---

## PRIOR CYCLE (committed to main) — mmap capture rebuilt to region-only + FN=0 (VM-verified)

The prior handoff had added a **whole-file eager capture at writable-section-create** (its "fix ③") on the premise
that the async mapped-page-writer flush "bails at APC_LEVEL" so per-region paging-write capture was unsound. **That
premise was wrong**, and the owner had always required *region-only* capture (whole-file dump forbidden). This
session proved it wrong via VM measurement and rebuilt the path.

- **Removed:** `SarMmapCaptureEager` (whole-file copy at section-arm) + its `MMAP_EAGER_TRIED` flag — **deleted**.
  mmap now stores **only the overwritten regions**, never the whole file. (This reverses the prior "fix ③".)
- **Added (load-bearing — do NOT remove):** a **UserMode arm gate** in `SarPreAcquireForSection`:
  `if (Data->RequestorMode != UserMode || ExGetPreviousMode() != UserMode) { release; return; }`. VM measurement
  showed the arm was over-triggering on **cache-manager data sections** (created KernelMode by `CcInitializeCacheMap`
  for ordinary buffered writes) → it armed every buffered-written file and double-captured. `Data->RequestorMode`
  is the true discriminator (Kernel for Cc, User for genuine `CreateFileMapping(PAGE_READWRITE)`).
- **Sole capture path now:** `SarPreWrite → SarMmapOnPagingWrite → SarMmapCaptureInline → SarRawReadStageRange`. At
  each paging-write flush (which names the exact dirtied sub-range) at PASSIVE, we raw-read that region's **on-disk
  pre-image** (still intact — the flush has not committed) and stage it. Region-only, FN=0.
- **Files changed (committed to main):** `driver/{operations.c, capture.c, capture.h, seam.h}`. No comments, no
  dead/fallback code. Reserve lifecycle intact: whole-file *bound* reserved at arm (the ENFORCE fail-closed
  guarantee); released-as-you-stage; remainder released at streamctx teardown (`driver/driver.c:65`).

### VM verification (durable evidence in `build_verify/AH*`)
Instrumented event-ring probe builds (since fully reverted) on the `SarTarget` VM:
- **A-H1 (IRQL):** 256/256 paging-write pre-ops at **PASSIVE_LEVEL** — including the *asynchronous* mapped-page-writer
  flush (IRP_PAGING_IO without SYNCHRONOUS), even under 768 MB memory pressure. Top-level IRP is SET at the seam.
  MS Learn/OSR corroborate (mapped-page-writer IRP_MJ_WRITE|PAGING_IO is normally PASSIVE; APC is the *separate*
  ACQUIRE/RELEASE_FOR_MOD_WRITE callback). ⇒ fix ③'s "APC bail" was overstated.
- **A-H2 (region-granular):** flush writes are ≤60 KB coalesced runs / single 4 KB pages — **never whole-file.**
- **A-H3 (pre-image correctness):** eager OFF, paging-write path only → target's 64 dirtied pages captured
  **64/64 = correct pre-image, 0 post-image, 0 garbage, region-exact.** The "other captured files" were traced to
  Cc data sections (KernelMode) — removed by the UserMode gate — plus a few genuine user mmaps by another process
  (correctly captured → allow-list territory).
- **Timing (why VM runs felt slow — NOT a hang):** `sarctl events 256` alone ≈ 246 s (the service↔driver comm-port /
  B.2 latency, ~1 s/event); PS-Direct file copy was fast (~2 s). Read few events in future runs.
- **FABLE5 final review of shipped code:** 5 items — **2 false alarms** (`SarMmapRangeCovered` already takes
  `cap_lock` shared; `SarMmapReleaseStaged` is a CAS loop), **1 empirically refuted** (the UserMode gate does NOT
  disable capture — A-H3 got 64/64 with the gate on), **1 style** (silent APC), **1 real-but-low-impact** (drift,
  below). Net: **no forced change.** (FABLE5 tends to over-flag; verify each against the actual code — it hedged
  the two false alarms as "iff … — confirm".)

### The APC boundary (owner-accepted; FABLE5 concurred) — do NOT build bounded-pend
A paging write arriving at APC_LEVEL (rare fault-collided/trim) hits the `KeGetCurrentIrql()!=PASSIVE` bail → no
capture → **fail-open.** Never observed (256/256 PASSIVE incl. under pressure); not attacker-controllable; its only
closure was whole-file eager (forbidden) since paging writes cannot be refused (MS/OSR: never FLT_PREOP_SYNCHRONIZE
async paging I/O). Bounded-pend would re-introduce the deadlock-adjacent machinery just removed, for a path that
never runs. **Decision: documented boundary (R-mmap-APC).** Revisit only if a field telemetry counter ever records
a non-PASSIVE mmap paging write.

### R-mmap-drift (low impact; boundary)
Extent map built once at arm, reused per flush; if clusters were relocated (defrag/FSCTL_MOVE_FILE) between arm and
flush, a raw-disk read could stage stale/other-file bytes. **Does not affect the FN=0 attack scenario** (defrag
does not run on a file under active mmap-encryption). A per-capture retrieval-pointer re-resolve closes it but adds
per-write FSCTL cost (hurts zero-usability); left as a boundary.

---

## RESOLVED this session — the two prior §DT tensions (fold into the Constitution)

- **DT-T1 (mmap over-capture defeated FP-minimization):** RESOLVED by the region-only redesign (§THIS SESSION). mmap
  no longer whole-file-dumps; it captures only overwritten regions at the paging-write flush. The prior framing ("mmap
  MUST be eager-at-arm because the paging path is unsound") rested on fix ③'s wrong APC premise — A-H1/A-H3 disproved
  it. The residual FP (capturing *benign* overwrites) is closed by **T1 (§4.1)**: apply the same diff-restricted D∧T
  the non-mmap path uses (now possible — at the flush we hold both pre-image and post-image).
- **DT-T2 (first-write-wins vs latest-legit under bounded storage):** RESOLVED as: keep **first-write-wins** the
  un-gameable floor (III.5.3); route the interleaved-legit corner to (a) **Oracle key recovery** (exact latest state
  when the key is captured, III.5.4) and (b) **exempting the legit writer** (a whitelisted app's edits create no holds
  → the first hold is the attacker's pre-image = An). The *key-missed AND interleaved AND within-retention* corner is
  an honest [BOUNDARY]. External research confirms it is irreducible: arXiv 2510.15133 (intermittent-encryption
  detection ceilings), Cerberus mimicry (defeats behavioral detectors), and the FTL time-travel systems (FlashGuard/
  Almanac/RSSD) show NO scheme gets both under a strict one-copy budget without a trusted detector — so a bounded
  first+latest **dual-anchor** is the field-settled minimal form, and if the owner wants latest-legit recovery it must
  be **operator-directed** (offer both anchors at restore; never auto-selected by T). Land as III.5.x [DECISION]/[BOUNDARY].

### Established facts the successor MUST carry (do not re-derive them wrong)
- **The diff-restricted T is NOT gameable by partial/intermittent encryption.** It measures novelty of ONLY the
  changed bytes (novel ÷ changed), not whole-file entropy — so encrypting *any* region, however small/intermittent,
  fires it (EXTERNAL_VALIDATION §4.1). The "T is gameable" caveat applies ONLY to the DT-T2 *version-selection*
  question (which retained copy is clean), NOT to *detecting a destructive encryption write*. This session's
  implementer briefly conflated the two and was corrected by the owner — **do not repeat.**
- **mmap new-write vs overwrite** is discriminated by **extent type**, not read-precedence: real extent (lcn≥0) =
  overwrite of existing data → preserve; sparse hole (lcn<0) / beyond `covered_len` = new-write, nothing destroyed →
  skip (`SarRawReadStageRange` stages zeros for sparse, harmless). Destruction-then-new-write (truncate/supersede then
  mmap) is caught by the truncate/supersede path, not the mmap write. (Non-mmap uses read-precedence; mmap can't see
  reads, so it uses "is there a real on-disk original".)

---

## 0. The north star — internalize, do not merely read (the whole project serves this)

Three goals, strict priority; **every** mechanism is the minimal thing that serves them:

1. **FN = 0** against a *recoverable* data-destruction encryption attacker. No file whose original a process **read**
   is ever left unrecoverable: its key is captured (**Oracle**) or its exact destroyed region is preserved **before**
   the write commits (**Preserve**). Under **ENFORCE** this is absolute: when neither can be guaranteed for a
   destructive op, the op is **refused** (fail-closed) — **and, as decided this cycle, refused rather than served by
   evicting a not-yet-superseded pre-image (block-before-evict)** — so the original survives.
2. **FP ≈ 0** — effectively invisible in normal operation. Blocks only on (a) Oracle forward proof, (b) Phantom
   conviction, or (c) the ENFORCE resource bound; (c) refuses the *individual op*, never the process. AUDIT never blocks.
   **Exempt identities are monitored ZERO (no Oracle, Preserve, capacity-refusal, or Phantom — the Gate never fires).**
3. **Cost → theoretical minimum** under (1) and (2).

If a change trades (1) for (3), (1) wins. Trades of (2) for (3) are the owner's to ratify.

---

## 1. Architecture as-built (grounding)

Three evidence channels (Oracle / Preserve / Phantom), response graduated to certainty; **response is NEVER process
termination** — every response denies the destructive I/O and the process keeps running. Gate (`engine/src/gate.c`) is
passive (`D ∧ read-preceded ∧ T`, T = diff-restricted 2-gram novelty; emits capture-or-skip, never blocks). Corrected
graduated response: **circumstantial capacity → per-op refusal, no process block, paging exempt; definitive conviction
(Oracle FORWARD / Phantom) → process block.** See the code + `DESIGN_REVIEW_PRESERVATION.md` for the backend
detail; `docs/CONSTITUTION.md` is the ratified, authoritative specification and matches this design.

---

## 2. What this cycle COMPLETED and VM-verified (do NOT redo; this is the settled grounding the ratified Constitution encodes)

The prior handoff's "NEXT WORK": **A (exemption), B (two side-findings), C (Constitution)**. Status now:

### 2.1 Exemption — "an absolute, unforgeable contract" — DONE (Tier-1 + Tier-2), VM-verified
Unified single predicate consumed by every mechanism. Two **unforgeable, injection-safe** Tier-1 anchors + the Tier-2
operator whitelist. The design was **derived from two owner requirements** (all-minifilter-Windows-version compatible;
forgery impossible or extreme-cost), externally researched (OSR + itm4n/PPL), and FABLE5-reviewed at code level:

- **L1 — Tier-1 object anchor (OS-owned target path), all-version.** `SarTargetExempt` (`driver/operations.c`) matches
  a **normalized** target against a fixed OS-owned prefix list (`\system32\config\`, `\wbem\repository\`, `\winevt\logs\`,
  `\logfiles\`, `\catroot2\`, `\serviceprofiles\`, `\softwaredistribution\datastore\`, `\inf\`) **anchored to the system
  volume** via `\SystemRoot` resolved once at DriverEntry (`SarOsAnchorInit` → `ObQueryNameString`). Cached as
  `SAR_STREAMCTX_FLAG_OS_OWNED` at `SarPostCreate`. **CRITICAL FIX this cycle (only VM testing found it):** files opened
  **before the driver loads** (the persistent OS hives/logs — B.1's real culprits) never hit `SarPostCreate`, so a
  create-time-only cache missed them. Fixed with a **lazy resolve at the write seam** (`SarStreamExemptTarget`): on the
  first PASSIVE op of a not-yet-checked stream, resolve the normalized name once and cache `OS_OWNED | OSOWN_CHECKED`.
  **VM-verified: OSOWN phase = 0 OS-owned preserve entries (B.1 CLOSED).**
- **L2 — Tier-1 subject anchor (PP/PPL), 8.1+, injection-proof.** `SarProcessProtectedTrusted` (`driver/processnotify.c`)
  queries `ProcessProtectionInformation` at process create; a protected process at signer WinSystem/WinTcb/Windows/
  Antimalware → `id_state = EXEMPT` at insert. Version-graceful (pre-8.1 → `INVALID_INFO_CLASS` → not exempt, no false
  trust). **Deliberately NOT anchored on mere signature** (a non-PPL signed process is injectable). Code-verified;
  not directly VM-tested because PP/PPL processes predate the driver load (the `procquery` mechanism works).
- **L3 — Tier-2 operator whitelist (strong identity) + the VERDICT WIRE (was the missing half).** Whitelist entries
  carry `content_hash + cert_subject` (image_path is now **excluded** from the match — FABLE5 C-1 fix, so path-form
  differences don't break matching). The runtime verdict path (previously dead — `ApplyVerdict` had no caller) is now
  wired: driver `MSG_PROCESS_QUERY` returns the kernel `start_key`; service `SAR_CTL_OP_VERDICT{pid}` does
  QueryFullProcessImageName → PROCESS_QUERY(start_key) → `sar_identity_evaluate` → `IDENTITY_VERDICT{pid,start_key,id}`;
  the driver **re-checks its own whitelist + binds `start_key`** (rejects `start_key==0`, C-2), so the driver is the
  policy authority (a lying service gains nothing it didn't already have via WHITELIST_ADD). sarctl gains `verdict`,
  `procquery`. **VM-verified: TIER2 = exempt process leaves 0 keys / 0 preserve / 0 blocks; control (non-exempt) IS
  monitored; FORGE (signature-valid but content-hash-mismatched copy) → NOT exempt.**
- **L4 — Phantom honors exemption.** `SarPhantomIsTrusted` returns TRUE for `id_state==EXEMPT`; the 3 per-write lock
  traversals merged into one `SarPhantomEvidenceConvicts` that early-outs on EXEMPT before recording evidence.
- **A.3 residual (owner-ratified):** a Tier-2 (non-protected) whitelisted app can be a confused deputy (injected/abused).
  This is *post-exemption abuse of a genuinely-trusted binary*, NOT forgery; it vanishes for Tier-1 (PP/PPL injection-proof).

### 2.2 Block-before-evict (owner-decided ①) — DONE, VM-verified
The synchronous in-place ENFORCE path already refuses (not evicts) on `SAR_STAGE_DROPPED`. This cycle **closed the two
seams FABLE5 proved still leaked**, plus the mmap reservation:
- **L5-A — mmap concurrent-section reservation.** `SarPreserveReserve/Release` + `reserved_bytes` (`driver/preserve.c`).
  Section-arm (`SarPreAcquireForSection`) atomically **reserves** est=filesize·17/16 (both AUDIT and ENFORCE; ENFORCE
  refuses the section on failure). Stages **release-as-you-stage** (`SarMmapReleaseStaged`, exact per-chunk, CAS-safe) so
  the peak charge is `est` not `est+staged`. `AgainstReservation` flag (`SarPreserveStage`) = backed streams use
  total-only admission. One-shot lazy re-reserve on the first ENFORCE stage closes the AUDIT-armed-then-ENFORCE window.
  **VM-verified: MMAP2 = concurrent oversized sections refused, ZERO loss; single mmap succeeds + captured (reservation
  released).**
- **L5-B — delete/rename hard-refusal.** `SarCaptureLinkPreserve` returns `SAR_STAGE_RESULT`; record-cap → DROPPED;
  hardlink-fail → **synchronous** `SarCaptureWholeContent` (data-scan section) → real result. `SarCaptureSubmit{Delete,
  RenameTarget}` propagate it; `SarPreSetInformation` refuses (FLT_PREOP_COMPLETE + INSUFFICIENT_RESOURCES) in ENFORCE.
  Dead `SarCaptureStageRegion` removed. Code-verified by FABLE5; **VM DELCAP SKIPPED** (needs a hardlink-less/FAT guest
  volume — `New-VHD` is unavailable in the guest; see §5).

### 2.3 FABLE5 review record (this cycle) — all converged
FABLE5 (a Fable-5 subagent, `model: fable`) reviewed at code level **three times**: (design → found 2 defects), (impl →
found the 2 residual seams L5-A/L5-B + LOW residuals), (fixes → "no data-loss path; block-before-evict holes closed").
Then it reviewed the **VM-verification design** and found **2 S1 verification holes** (both fixed): **A-1** REFUSED must
be corroborated per-file (transform emits `OK <path>` = ATTEMPTED; classifier asserts `REFUSED ⊆ ATTEMPTED`); **B-1**
"two equal reads" is false-quiescence → **flush each target (force lazy write-back) + inflight==0 gauge**; plus **C-1**
(identity_equal path-bound → hash+signer only) and **D-1** (FORGE must flip the Authenticode-excluded PE CheckSum at
`e_lfanew+0x58`, not absolute 0x58). **Framing note: FABLE5 is conservative on cybersecurity terms — describe the driver
neutrally (reversible data-preservation / versioning-undo filter + allow-list access mediation), avoid "ransomware".**

### 2.4 Verification infrastructure added this cycle
- `driver` enablers: `MSG_PROCESS_QUERY`/`PROCESS_REPLY` + `SarStateIdentityQuery`; `capture_inflight` in `STATUS_REPLY`
  + `SarCaptureInflight` (B-1 gauge, **currently blocked by B.2 — see §4**).
- `service`: `SAR_CTL_OP_VERDICT`, `SAR_CTL_OP_PROCESS_QUERY`, `SAR_CTL_OP_STATUS`.
- `tools/sarctl.c`: `verdict <pid>`, `procquery <pid>`, `inflight`.
- `tests/harness/stream_transform.c`: optional 7th arg `preHoldSeconds` (destroy after a hold, so a verdict can land
  first) + prints `READY pid=`. `tests/harness/mmap_over.c` (**new**): maps an existing golden file PAGE_READWRITE,
  overwrites, holds the section open, prints `OK`/`REFUSED`.
- `scripts/vm_verify_new.ps1` (**new**): restore → deploy → 8 phases (S, F2, G-strong, MMAP2, DELCAP, TIER2, FORGE,
  OSOWN) with the **hardened classifier** `ClassifyCorpus` (A-1 ATTEMPTED, provenance-bound recover, set-closure) and
  `DrainBarrier` (B-1 flush + inflight-or-settle).

### 2.5 Files changed (working tree, UNCOMMITTED — this cycle stacks on the prior uncommitted F0/fail-closed/preserve work)
`driver/`: operations.c, seam.h, preserve.c, preserve.h, capture.c, capture.h, driver.c, state.c, state.h,
processnotify.c, phantom.c, phantom.h, commport.c · `common/include/semantics_ar/protocol.h` ·
`control/src/whitelist.c`, `control/src/msg.c` · `service/control.c`, `service/control.h` · `tools/sarctl.c` ·
`tests/harness/stream_transform.c`, `tests/harness/mmap_over.c` (new) · `scripts/vm_verify_new.ps1` (new) · this file.

---

## 3. Verification state
- **Host (Release):** `test_engine` 77/0, `test_capture` 54/0, `test_recover` 97/0 (unchanged; cipher math reused, not
  re-paid in the VM).
- **Driver:** `scripts\build_driver.bat` → clean (C4083 vcruntime = SDK noise; also pre-existing C4244/C4018 in phantom.c
  bucket-hash, not new). **NOTE: the build WIPES `build_driver\`, so re-sign the pkg after every driver rebuild.**
- **Usermode:** `cmake --build build_win --config Release` → clean (exit 0). Deploy exes from `build_win\...\Release`.
- **Harness:** `scripts\build_harness.bat` → clean. **stream_transform.exe MUST be re-signed after every harness rebuild
  (the rebuild strips the Authenticode signature; TIER2/FORGE need it signed with the test cert).**
- **VM (`scripts\vm_verify_new.ps1`), this session, clean `clean-baseline-20260704` restores:** the regression set
  (S/F2/G-strong/MMAP2/TIER2/FORGE/OSOWN, 27 asserts) stayed green across every run. The new **Phase MMAP-ORACLE**
  passed its Oracle/block asserts every run (byKey recovery, ENFORCE block-forward, negative control not
  convicted) but its floor-completeness assert (`lost -eq 0`) failed consistently — this is the mmap multi-file
  gap above, an honest FAIL, not a flaky one. Best full-suite result this session: **35 passed / 1 failed / 0
  skipped.** SKIP for DELCAP did not occur this session (FAT volume prep succeeded each run). **The broad
  `vm_verify_coverage.ps1` (A/A2/P/E/EV/B/C/C2/H/D) was still NOT re-run — do it (§4.3).**
- **Events-query fix verified:** `sarctl inflight`/`sarctl events` no longer block indefinitely; full-suite VM
  runs went from routinely stalling to completing in normal time once this landed. See "Events-query fix" above.
- **T2 (this session), `scripts\vm_verify_t2.ps1`:** **13 passed / 0 failed / 0 skipped**, crash-free soak, no
  launch FP. Covers ObGuard access-bit stripping (process + thread + DuplicateHandle-on-recipient), AUDIT-vs-ENFORCE,
  Tier-1 vs Tier-2 trust asymmetry, auto-verdict launch-detection, and the interpreter denylist. `build_verify/
  semantics_ar.sys` + `semantics_ar_elam.sys` are the verified build artifacts from this run; `tests/harness/
  inject_probe.c` is the new trust-signed harness exercising untrusted-opener handle requests against exempt
  targets. **The broad `vm_verify_coverage.ps1` regression sweep is still NOT re-run since T1+T2** — see BACKLOG.

---

## 4. NEXT WORK (strict order) — the terminus is the full frontend redesign

### 4.1 T1 — Unify the mmap and non-mmap capture logic onto ONE wiring (owner's priority; the "화룡점정")
**STATUS: DONE, COMMITTED, VM-verified** (`vm_verify_new.ps1`: 29 passed / 0 failed / 1 env-skip; MMAP-ORACLE
positive lost=0 — three FN=0 defects found and fixed, see §PRIOR SESSION for the full account). Do NOT
re-implement or re-litigate this — the design below is now what is shipped, and is encoded in the ratified
`docs/CONSTITUTION.md` (§T3 COMPLETE); kept here only as grounding.

The owner dislikes the mmap/non-mmap split and wants them on the **same** gate→preserve→Oracle wiring. This is now
possible because at the mmap flush we hold **both** the pre-image (raw-read from disk) and the post-image (the
paging-write buffer) — exactly the (old, new) pair the non-mmap gate consumes.
- **Do:** at the mmap flush, for **real-extent (overwrite) regions only** (skip sparse/new per §RESOLVED), compute the
  **diff-restricted D∧T** (`sar_gate_classify`, `engine/src/gate.c`) on (pre-image, post-image); on fire → preserve the
  region **and invoke the Oracle** (snapshot `mmap_arm_pid`'s heap, scan for the key, forward-prove `Enc_K(pre)==post`,
  incl. the σ-constant "expand 32-byte k" stream scan). This gives mmap attacks (Babuk/Maze — a major vector) the
  **unbounded key-recovery channel they currently lack** (today mmap is preserve-only = bounded), and makes mmap
  preservation **targeted** (only encryption-like regions), closing DT-T1's residual FP symmetrically with in-place.
- **Binding constraints:** MIRROR the exact non-mmap `SarCaptureSubmitWrite → SarCaptureWorker` gate/preserve/Oracle
  wiring — do NOT invent a parallel path. **T gates the ORACLE, not the preservation FLOOR** — preservation must stay
  unconditional for read-preceded / real-extent destruction so a high-entropy original (compressed/media, where T is
  quiet) is still preserved. First read how the non-mmap synchronous `SarCaptureInPlaceRegion` (D∧read floor, ungated
  by T) and the T-gated async Oracle are split, and replicate that split for mmap. Timing caveat: mmap flush is async →
  the key may already be zeroized (best-effort, backstopped by the preservation we already do). End state: **mmap and
  non-mmap are one path**; the only mmap-specific pieces are the seam (paging-write vs IRP_MJ_WRITE) and the
  new/overwrite discriminator (extent-type vs read-precedence). VM-verify (A-H1..A-H3 evidence + a mmap-Oracle test).

### 4.2 T2 — Whitelist + active injection-proofing
**STATUS: DONE, VM-verified** (`scripts\vm_verify_t2.ps1`: 13 passed / 0 failed / 0 skipped — see §T2 COMPLETE at
the top for the full record: `driver/obguard.c`, the trust model decision, auto-verdict, the two dead-ends that
must not be retried, and the R-t2-window/PPL-ceiling/A.3 boundaries). Kept below only as the original design
rationale, now encoded in the ratified `docs/CONSTITUTION.md`.

Exemption = ZERO monitoring (contract). To make it sound without whole-file trust, the driver **actively makes an
exempted process injection-proof** so a non-kernel actor cannot write through it. (Research this session: real
ransomware DLL-sideloads / injects into signed processes — e.g. REvil into MsMpEng.exe — so trusted *identity* is not
enough; the exemption anchor must be an injection-proof *STATE*.)
- `ObRegisterCallbacks` on PsProcessType/PsThreadType stripping only manipulation bits (PROCESS_VM_WRITE / VM_OPERATION
  / CREATE_THREAD / DUP_HANDLE / SUSPEND_RESUME / SET_INFORMATION; THREAD_SET_CONTEXT / …) from **untrusted** openers;
  keep QUERY / TERMINATE / VM_READ / SYNCHRONIZE (zero observer impact — Task Manager / monitors / crash-read still
  work). **DuplicateHandle must check the TARGET process, not the caller.** Protected-set keyed by `EPROCESS*` +
  `PsGetProcessStartKey` (NEVER PID — PID-reuse UAF/alias). Add a pre-existing-handle sweep (ZwQuerySystemInformation +
  DUPLICATE_CLOSE_SOURCE). Signature verdicts must be **cached at process-create/first-image-load**, never computed
  synchronously in the Ob callback.
- Sideloading: minifilter veto of a non-cosigned image mapped into a protected process, and/or revoke exemption on a
  foreign-signed module load (load-image notify fires before DllMain).
- **Scope exemption to "headless, non-scripting" apps** (no untrusted-code execution) — that is where forced
  injection-proofing has zero usability cost. (For arbitrary GUI/scripting apps, do NOT full-exempt; the earlier
  "cost-only monitoring" idea is REJECTED — exemption is exemption.)
- Honest [BOUNDARY] (same ceiling PPL has, IX.1): kernel attacker; in-process memory-safety exploit; a voluntarily
  mapped shared-writable section the app executes from.

### 4.3 T3 — Constitution full rewrite
**STATUS: DONE.** `docs/CONSTITUTION.md` is ratified and is the authoritative specification governing the
implementation; see §T3 COMPLETE near the top of this file. Kept here only as the historical order marker —
T1+T2+T3 are all done; §4.4 is the active terminus.

### 4.4 T4 — Full frontend redesign (THE TERMINUS — ACTIVE NOW, top priority; T1+T2+T3 are all done)
**See §T4 MISSION (ACTIVE) near the top of this file for the full framing — this subsection is the condensed
pointer.** The operator-surface frontend under `frontend/` (`SemanticsAr.App`, `SemanticsAr.Core`, `sarapi`,
`elevation-host`) was built against the legacy Constitution's operator-surface design and must not be assumed
valid. Redesign it from scratch, derived from the ratified `docs/CONSTITUTION.md` and
`docs/EXPERIENCE_CHARTER.md`, independently re-analyzing both governing documents and the existing frontend
source before designing anything. Reuse only what genuinely fits the new design; boldly discard the rest.

---

## 5. VM / build / sign / gotchas (bake into any harness)
- **VM `SarTarget`** (Hyper-V Gen2, Win11, admin/admin, PS Direct). Single checkpoint `clean-baseline-20260704` —
  **restore-only**; trusts the test cert (thumbprint `1E4B3044AAE3A92E4DEB615F350DB122F0AEF114`, in host
  `Cert:\CurrentUser\My`, CN=SemanticsAr Test).
- **Restore ceremony (flaky):** ALWAYS `Stop-VM -TurnOff -Force; Start-Sleep 6` BEFORE `Restore-VMSnapshot`; then
  `Start-VM`; wait ~45 s; verify PS Direct with a retry loop. **`vm_verify_new.ps1` does this** (`-SkipRestore` to skip;
  but **-SkipRestore is NOT a clean baseline** — it caused broad regressions [stale store/cert] — prefer a full restore).
- **Build driver:** `scripts\build_driver.bat` → `build_driver\{semantics_ar.sys, semantics_ar_elam.sys}` (**this wipes
  `build_driver\`**). Then the **minimal signer** (package_driver.ps1 is BROKEN): mkdir `build_driver\pkg`; copy .sys +
  elam + `driver\semantics_ar.inf`; `Export-Certificate` the cert to `pkg\SemanticsArTest.cer`; `signtool sign /fd
  sha256 /sha1 <thumb> pkg\semantics_ar.sys` (+elam); `Inf2Cat /driver:pkg /os:10_X64,10_GE_X64`; `signtool sign ...
  pkg\semantics_ar.cat`. signtool = `Win Kits\10\bin\10.0.26100.0\x64\signtool.exe`, Inf2Cat = the `x86` one. **Re-sign
  the pkg after EVERY driver rebuild.**
- **Build usermode:** `cmake --build build_win --config Release`. **Adding a field to a shared wire struct** (e.g.
  `sar_posture_frame_t`) breaks a frontend `_Static_assert` in `frontend/sarapi/src/posture_client.c` — keep the posture
  frame ABI stable; expose new data via a control op instead (that's why `capture_inflight` went through `SAR_CTL_OP_STATUS`,
  not the posture frame).
- **Build harness:** `scripts\build_harness.bat` (out = `build_harness\`). **Re-sign `stream_transform.exe`** after.
- **Deploy** (see `vm_verify_new.ps1`): unload + clear `C:\Windows\System32\drivers\SemanticsAr\*` + copy pkg +
  `certutil -addstore Root/TrustedPublisher` + pnputil + set service reg + `fltmc load` + start service. The `0x801f0013
  could not unload / filter not found` line on first deploy is benign. A redeploy resets driver state.
- **Harness/host gotchas (reuse):**
  - Run VM PowerShell with `dangerouslyDisableSandbox`. A pre-execution guard flags any command whose TEXT contains
    `Remove-Item` near `C:\Program...` **or backslash path patterns** — avoid `Remove-Item` (use `New-Item -Force` /
    overwrite / redirect) in such commands.
  - **budget unit is MB** (`sarctl budget <retention-sec> <capacity-MB>`), but propagation is flaky (B.2) — set it a
    few times with short sleeps and let it settle before arming a section; do NOT assume it applied immediately.
  - **`sarctl inflight` currently returns empty (B.2)** — the `DrainBarrier` handles this (flush + fixed settle fallback).
  - **Provenance binding:** every corpus reuses `sv_%05d.dat`, so match keys/regions by **dir-leaf + filename**, not bare
    filename (FABLE5 identity-binding hole #4). `ClassifyCorpus` does this.
  - **A-1 ATTEMPTED:** the classifier only scores a golden file REFUSED if the transform reported it ATTEMPTED (parse
    `OK <path>` from stdout); a golden-but-un-attempted file is a suite error, never silent REFUSED.
  - **mmap classifier limit:** `ClassifyCorpus`'s single-region `preserve-recover` cannot reassemble a MULTI-region mmap
    capture — verify mmap capture by **region existence** in `preserve-list` (as MMAP2's release step does), not full
    recover, unless you add a multi-region recover helper.
  - **FORGE:** the PE optional-header CheckSum is at **`e_lfanew + 0x58`** (relative; `e_lfanew` at abs 0x3C), and is
    Authenticode-excluded — flipping it keeps the signature valid but changes the full-file SHA-256. Flipping absolute
    0x58 corrupts the DOS header and breaks the signature.
  - **TIER2 timing:** launch the signed transform with a `preHoldSeconds`, get its PID, `sarctl verdict <pid>`, confirm
    `procquery id_state=2(exempt)` BEFORE the hold elapses and destruction runs.
- `docs\DEBUGGING.md` = kernel-dump runbook; `docs\TESTING.md` = tiering; run the VM under Driver Verifier (FS filter
  verification / Special Pool / Force IRQL / Low Resources) for a serious pass, and prove the oracle can go red
  (negative control) before trusting a green.

---

## 6. Binding rules (do not repeat past mistakes)
1. **Code is written WITHOUT comments. No dead/compat/fallback code.**
2. **The project has never shipped** — no migration code, no schema bumps.
3. **VM-verify every nontrivial driver change.** This cycle's L1 defect (boot-opened OS files un-exempted) was invisible
   to review and FABLE5 and only VM testing (OSOWN 48→0) exposed it. Never trust a review (or this handoff, or FABLE5)
   uncritically.
4. **Never trust data from a dead driver** — assert `fltmc filters` shows `semantics_ar` at each measurement.
5. **Gate passivity is inviolable**; active response is downstream of a verdict or the ENFORCE bound; the resource-bound
   response is a **per-op refusal via block-before-evict**, never a process block, never an eviction of a live pre-image.
6. **FN=0 outranks cost; a mechanism that serves FN=0 but violates FP≈0 catastrophically (self-DoS) is a defect.**
7. **Exemption is an absolute contract**: an exempt identity is never monitored, blocked, or taxed (the Gate never
   fires). Invest in making exemption **unforgeable** (OS-owned normalized path ∪ PP/PPL ∪ authenticated strong-identity
   verdict with driver-side start_key binding), NOT in watching the exempt. A forgeable anchor (token=SYSTEM, bare path/
   name, unverified signature) is never an exemption basis.
8. **FABLE5 reviews are code-level with counterexamples** (design → impl → VM re-verify). Use neutral framing.
   FABLE5 **over-flags** — it will always claim a problem. Verify each finding against the actual code; it hedges
   uncertain ones ("iff … — confirm"). This session it raised 5, of which 2 were false alarms (code already locked /
   already atomic), 1 was empirically refuted, 1 was style. Reflect critically; do not blindly apply.
9. **NEVER satisfy a hard requirement by silently substituting an easier, owner-forbidden design.** The recurring
   failure this session was exactly that (whole-file mmap dump; monitoring the exempt; blocking mmap; a wrong
   "T-gameable" claim). When a requirement seems impossible: SOLVE it, or state the precise irreducible reason —
   grounded in an empirically-verified or primary-sourced fact, never a guess. The requirements are load-bearing:
   FN=0; region-only (never whole-file); exemption = zero monitoring; zero usability impact; transcend the frontier.
   When a load-bearing fact is ambiguous mid-implementation, do targeted web research BEFORE continuing.
10. **A passing test can hide a real defect if the test setup has its own accidental rescue path.** This session's
    mmap multi-file batch test showed FN=0 on one run purely because the harness reused one key/keystream across
    every file in the batch, letting the one Oracle-recovered key silently substitute for a missing floor on the
    others; the automated classifier never even attempted `recover` on the other files. Passing is not evidence
    until the reason it passed is understood. When instrumenting an unexplained result on the actual driver, add
    counters at each decision point and read them **before** attempting a fix — two fix attempts made against an
    unconfirmed guess this session both failed to close the gap, and the bolder one (a retry loop with a blocking
    wait added to a paging-write-adjacent path) made the reproduction rate measurably worse. Prefer a full VM
    snapshot restore over repeatedly hot-swapping a live driver while a root cause is still unconfirmed — the
    live VM stopped responding once during exactly that kind of iteration this session, cause undetermined.
11. **NEVER build a destructive pre-existing-handle sweep (retroactive `ZwDuplicateObject(DUPLICATE_CLOSE_SOURCE)`
    against handles enumerated via `SystemExtendedHandleInformation`).** T2's implementer built and VM-tested one
    to close the pre-verdict handle-staging window; it force-closed handles held by pre-load CRITICAL OS processes
    (csrss/services) and **produced a reproduced CRITICAL_PROCESS_DIED bugcheck**, caught only by post-run uptime
    monitoring (the synchronous asserts all passed — this is rule #10 again). It was removed; its unique value is
    now covered by auto-verdict's ~250 ms window collapse. Do not reintroduce this class of mechanism.
12. **NEVER attempt birth-time (process-create-notify) handle-surface protection in kernel for a whitelist
    candidate.** T2's implementer tried provisionally protecting a not-yet-verdicted process's handle surface at
    creation so untrusted openers would be stripped before verdict; it reliably **broke process creation itself**
    (STATUS_ACCESS_DENIED launching the app) because the OS's own creation machinery (parent completing
    NtCreateUserProcess, csrss/subsystem finalizing the newborn) needs manipulation-grade handles to the new
    process — the same "don't strip OS-infrastructure handles" hazard class as rule #11's bugcheck. PP/PPL gets
    birth-time protection for free only because the OS implements PPL creation itself; a third-party driver
    cannot replicate it for arbitrary whitelisted processes. Exemption/handle-surface protection legitimately
    starts POST-verdict; close the window at the source (fast auto-verdict), not by protecting a newborn process.
