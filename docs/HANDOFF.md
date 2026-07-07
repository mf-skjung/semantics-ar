# Handoff — semantics-ar — THIS SESSION rebuilt the mmap capture path to **region-only, FN=0, zero-usability-impact** (removed whole-file eager capture; added a UserMode arm gate), VM-verified end-to-end (A-H1 IRQL=PASSIVE 256/256; A-H2 region-granular ≤60 KB; A-H3 pre-image 64/64 correct). COMMITTED to main. OPEN and specified below: **(T1) unify the mmap and non-mmap capture logic onto ONE wiring — diff-restricted D∧T → preserve + Oracle** (the owner's "화룡점정"); **(T2) whitelist + active injection-proofing**; **(T3) full Constitution rewrite — the terminus.**

This is the single living handoff — a **specification for the next work**, not a log. **Read §THIS SESSION,
§RESOLVED, and §4 NEXT WORK below first — they are newer than everything under Part 0+ and supersede it where they
conflict.** This session's work is **COMMITTED to main** (mmap redesign in `driver/{operations,capture,seam}` +
VM evidence under `build_verify/AH*` + probe scripts). Before touching anything, read
`docs/DESIGN_REVIEW_PRESERVATION.md` (adopted preservation design), `docs/EXTERNAL_VALIDATION.md` (frontier; §4.1 =
the diff-restricted-T soundness), and the VM evidence `build_verify/AH1_AH2_analysis_20260707.md` +
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

## THIS SESSION — mmap capture rebuilt to region-only + FN=0 (done, committed, VM-verified)

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
(Oracle FORWARD / Phantom) → process block.** See the code + `DESIGN_REVIEW_PRESERVATION.md` for truth; `CONSTITUTION.md`
is still legacy (whole-file-at-open) pending the §C rewrite.

---

## 2. What this cycle COMPLETED and VM-verified (do NOT redo; understand for the Constitution rewrite)

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
- **VM (`scripts\vm_verify_new.ps1`): 27 passed / 0 failed / 1 skipped** on a clean `clean-baseline-20260704` restore.
  Green: S/F2 (σ-scan capture, FN=0, A-1), **G-strong** (ENFORCE capacity → refused 20 / lost 0 / block-capacity),
  **MMAP2** (concurrent oversized sections refused, lost 0; single mmap captured, reservation released), **TIER2**
  (verdict → exempt, 0 keys/preserve/blocks; control monitored), **FORGE** (hash-mismatch → not exempt), **OSOWN**
  (B.1 closed, 0 OS-owned entries). SKIP: DELCAP (§5). **The broad `vm_verify_coverage.ps1` (A/A2/P/E/EV/B/C/C2/H/D) was
  NOT re-run this cycle — do it (§4.3).**

---

## 4. NEXT WORK (strict order) — the terminus is the Constitution

### 4.1 T1 — Unify the mmap and non-mmap capture logic onto ONE wiring (owner's priority; the "화룡점정")
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

### 4.2 T2 — Whitelist + active injection-proofing (design agreed this session; implement + VM-verify)
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

### 4.3 T3 — Constitution full rewrite (THE TERMINUS — last, only after T1+T2)
`docs/CONSTITUTION.md` III.5 / Part IV are legacy (whole-file-at-open). Rewrite to the running, VM-verified design.
**Binding (owner explicit):** deep understanding first (explain *why each clause is the minimal correct thing*;
re-derive / web-research if unsure); move only implemented logic in, each clause grounded in file/function + rationale;
**as-if-original** (no changelog); item-type discipline ([INVARIANT]/[DECISION]/[BOUNDARY]/[NEGATIVE]/[DEPLOY],
MEASURED/DERIVED/DESIGN), Part 0 closure, Part IX boundaries. **Must encode:** the unified gate→preserve→Oracle wiring
(T1); mmap as region-only paging-write capture with the UserMode arm gate, extent-type new/overwrite discriminator,
reservation + release-as-you-stage, and the APC + drift boundaries; first-write-wins + Oracle-reconcile +
probation/protected pools (DT-T2); the exemption contract with active injection-proofing (T2); σ-scan stream recovery;
the corrected graduated response (circumstantial→per-op refusal / block-before-evict / paging exempt; definitive→process
block). **Part IX residuals:** R-mmap-APC (paging unrefusable), R-mmap-drift (defrag-while-mapped), R-mmap-resident
(MFT sub-~700 B, no extents), A.3 Tier-2 confused-deputy, the bounded envelope, store confidentiality without a
kernel attacker, B.2 (comm-port latency — still open, visibility only).

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
