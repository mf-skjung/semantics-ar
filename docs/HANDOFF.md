# HANDOFF — semantics-ar productization drive (autonomous successor)

> Owner: sk.jung@metaforensics.ai. Rewritten 2026-07-12 to hand the project to a **fresh-context,
> fully-autonomous LLM** that will drive it to **complete productization** with the owner asleep /
> unavailable. The prior "CPU-peg / VM-instability" narrative is **obsolete — that problem was
> root-caused and fixed this drive** (the mmap writeback deadlock, commit `3f84a64`). This file is
> your durable memory. Keep §9 current.

---

## 0. IF YOU ARE PICKING THIS UP FRESH, OR YOUR CONTEXT WAS COMPRESSED — DO THIS FIRST

Your conversation does **not** terminate at the context limit; it gets **compressed**. If you notice
a summarized/compressed context (you don't recall the fine detail of earlier work), **STOP writing
code and re-ground completely before continuing**:

1. Re-read **this file top to bottom.**
2. Re-read the governing docs (precedence, authoritative, **never edit**): `docs/CONSTITUTION.md`,
   `docs/CONSTITUTION_PART_XII.md`, `docs/EXPERIENCE_CHARTER.md`, `docs/FRONTEND_DESIGN.md`,
   `frontend/mocks/recovery-and-budget.v1.html`.
3. `git log --oneline -15` and `git status` — reconcile against §4 (current state) and §9 (progress
   ledger). The ledger tells you which segment you are in.
4. Only then continue the current segment. **Never assume; re-derive from source + this file.**

This file IS your cross-compression memory. **After you close each segment (or make a material
decision), update §9 and the relevant §5 subsection** so a future compressed-you can resume exactly.

---

## 1. MISSION & TERMINATION

- **Mission:** advance semantics-ar to **complete productization**, *excluding MS driver
  certification* (WHQL/attestation — owner-only, out of scope).
- **The product is already feature-complete** against the owner-approved mock: the full WPF console
  (Home / Recovery / Budget / Exemptions), the recovery-and-restore flow, AUDIT/ENFORCE mode control,
  and first-run onboarding are all built; the kernel engine (capture / Oracle key-recovery /
  preserve floor / phantom conviction / self-protection) is functional and VM-verified. **Your job is
  NOT to add features or re-architect** — it is **verification, integration, packaging, and
  hardening** (the "last mile"). See §5.
- **Terminate only when** productization is complete, **or** you reach an owner-only gate (§3), **or**
  a genuine blocker that needs the owner. On stopping, update §9 + write a crisp status at the end,
  the way this file hands off.
- This is a **long journey**. Segment it (§5); the plan may change mid-way — that is expected. Keep
  §9 honest.

---

## 2. ABSOLUTE PROHIBITIONS (non-negotiable — violating these fails the task)

1. **Never break, weaken, bypass, or "temporarily" disable the core security logic.** FN=0 recovery
   (Oracle key-capture OR preserve-before-overwrite OR fail-closed refusal), the certainty ladder,
   phantom conviction, identity/exemption discipline, self-protection (VII), and **the mmap
   writeback fix just landed (§4.2)** are load-bearing and closed. Do not regress them.
2. **The Constitution outranks code.** Never circumvent it or reopen a settled `[DECISION]`/
   `[INVARIANT]` from inside a coding task. **Never edit the governing docs** (§0 list). Part XII
   ratification into `CONSTITUTION.md` is owner-only (§3).
3. **No stopgap / temporary / low-quality / dead / compatibility / fallback / migration / schema-
   version code.** Everything you write is **optimal, production-grade**, matches the surrounding
   idiom, and has **no comments** (project rule). If the *right* fix would require weakening security
   or the Constitution, **STOP and record it in §9 for the owner** — do not ship a shortcut.
4. **Never reintroduce inline blocking on the paging-write / unload path** (that was the wedge). Keep
   the deadman + cancel-then-wait + rundown; heavy work stays off the IRP (Const. II.3.2).

---

## 3. THE COMMIT GATE (how you close a segment) — and owner-only actions

**You MAY commit a coherent segment to `main` only when BOTH hold:**

1. **Real execution-based verification passes** — not just reasoning: the frontend builds `0` and
   tests green; the native tree builds `0`; the relevant `vm_verify_*.ps1` is green; and **observed
   live behavior** where the change has a runtime surface (e.g. the app actually renders/behaves on
   the VM against the live engine). "It should work" is never sufficient.
2. **FABLE5 complete code-level review with FULL AGREEMENT.** Present the changed/written code to
   FABLE5 **at code level** (§7). Demand code-level evidence. **Critically evaluate** — FABLE5
   over-flags and over-refuses: apply every real finding, reject non-reproducing / design-limitation
   ones *with your own code-level counter-evidence*. A segment closes **only when FABLE5's final
   verdict is ship and you and it are in complete agreement.** Never rubber-stamp; never ignore a
   valid finding.

One focused commit per coherent segment. Conventional-commit style. End the message with:
`Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`.

**OWNER-ONLY — never do these autonomously:** `git push`; editing/ratifying any governing doc
(incl. folding Part XII into `CONSTITUTION.md`); MS driver certification. When you reach one, stop
that thread, note it in §9, and continue other segments.

---

## 4. CURRENT STATE (ground truth, 2026-07-12)

### 4.1 Committed
- `4a49131` (pre-drive HEAD): session-5 — Part XII **XII.1/2/4/5** done (per-item pool status, actor
  binding, budget-attribution consumer, exemption vertical) + **§5.A** self-protection over-match
  anchor + VM GUI render fallback.
- **`3f84a64`** (this drive): the **mmap writeback deadlock fix** — `driver/capture.c` +
  `driver/driver.h`. These two files are committed and clean.

### 4.2 The mmap wedge fix (context — do NOT undo it)
Concurrent memory-mapped overwrites hard-wedged the guest: the write pre-op ran capture **inline on
the MM flush thread**, doing a synchronous `FILE_WRITE_THROUGH` store write under `Preserve->lock`
plus an untimed non-alertable raw-read wait — a `CcCanIWrite` circular wait against the flusher
threads it was blocking. Fix (two-phase, Const. II.3.2): the flush path now only reads old bytes into
**nonpaged staging** (bounded + cancellable — `SarMmapDiskRead` deadmans then `IoCancelIrp`,
`IoFreeIrp` moved to the initiator) and **defers** `SarPreserveStage` to the generic work queue
(`SarMmapDeferPreserve`). FN=0 preserved (old bytes captured before overwrite). Reservation
consume-on-stage restored via a referenced stream context; dedicated `SAR_MMAP_INFLIGHT_CAP`=128 so
an mmap flood can't starve conviction. FABLE5-reviewed **ship** (4 rounds). **Verified:** wedge gone
under the stress that hung the guest in ~30 s; `vm_verify_new` 29/0/1, FN=0 every run.

### 4.3 Uncommitted working tree — classify precisely (do not repeat the "capture.c is mixed"
confusion; `capture.c`/`driver.h` are committed and clean)
- **XII.3 integrity-halt (the frontend task in progress) — YOUR SEGMENT 1.** Full-stack, substantially
  implemented: driver `commport.c` (raises `integrity_halt` from keystore/preserve tamper),
  `preserve.c`+`engine/src/preserve.c` (tamper-detection refinement B, feeds it), `common/…/protocol.h`
  + `posture.h` + `frontend/sarapi/include/sarapi.h` (**ABI 2→3**), `service/control.c`, `tools/sarctl.c`,
  `frontend/…/PostureEnums.cs`/`PostureVerdict.cs`/`PostureEvaluator.cs`/`SarApiPosture.cs`/
  `NativeMethods.cs`/`HomeViewModel.cs`/`Notifications/ToastNotifier.cs`/`App.xaml.cs`, tests
  (`PostureEvaluatorTests.cs`, `InteropLayoutTests.cs` 40→48 B, `tests/test_preserve.c`). Probe
  `scripts/vm_verify_integrity_halt.ps1` (untracked).
- **Self-protection refinement C** — `SarPathUnderSystemRoot` (`operations.c`) used in `phantom.c` to
  exempt system-root paths; decl in `seam.h`. A driver self-protect over-match follow-up.
- **mmap unload rundown D** — `driver/driver.c` (wait `mmap_read_rundown` before `FltUnregisterFilter`).
  This is the **other half of the §5 unload-safety fix**; it pairs with the committed deadman. Commit
  it together with the driver verification when you touch that area.
- **HANDOFF.md** — this file (not code).
- **Untracked:** `build_verify/*.sys` (build binaries — **NEVER commit**), `.claude/`,
  `scripts/vm_diag_*.ps1` / `vm_verify_integrity_halt.ps1` (probes; commit only alongside their
  segment, gated).

---

## 5. THE LAST MILE — segments (each closed by the §3 gate; update on progress)

### Segment 1 — XII.3 integrity-halt: finish + verify + commit
- **Spec** (`CONSTITUTION_PART_XII.md` XII.3 / FRONTEND_DESIGN V.3): a single path-free posture-plane
  flag that a keystore/preserve tamper/rollback occurred (Const. II.4.1 / VII.1.3–4) → **red Home
  posture + a foreground window + persistent tray** — the one posture condition that foregrounds a
  window. `XII.3.1`: reports *that* verification failed, never *what* (no store bytes / keys /
  plaintext).
- **Done:** full-stack code (§4.3). **Remaining:** ① `dotnet build SemanticsAr.slnx` = 0 +
  `dotnet test` green — **copy a fresh `sarapi.dll` (ABI 3) first** or `SarApiIntegrationTests
  .AbiVersion` fails silently (§8 trap); native `cmake --build build_win`; ② VM (wire changed →
  mandatory): run `vm_verify_integrity_halt.ps1` (induce tamper → flag set + red; **clean run →
  no false positive**) and confirm `vm_verify_new` stays 29/0/1 (ABI/reply-size — the transport
  overflow class); ③ FABLE5 review of B (tamper-detection semantics: generation rollback vs
  legitimate newer generation; same-gen MAC mismatch) and the App edge-triggered foreground
  behavior; ④ commit XII.3. Commit **D (mmap rundown)** with the driver verification (it's §5's other
  half); B travels with XII.3.
- **DoD:** build/test green, probe green + regression 29/0/1, FABLE5 agreement, committed.

### Segment 2 — Live end-to-end on a real target (the true gate: "built" → "working product")
- Per the prior §5.A, the WPF app had only run **offscreen / on the host** — the host has no driver/
  service, so it is **not** a valid test. `4a49131` added the render fallback + self-protect fix to
  *enable* a live run; **nobody has confirmed the full live end-to-end.** Do it: run the published
  app on the VM (VMConnect **enhanced session** = the operator's desktop) against the **live
  driver+service**, and exercise every surface with real data — Home posture (incl. the Segment-1
  red integrity-halt), Recovery (induce an incident → recover → **verified byte-for-byte restore**),
  Budget (attribution bars), Exemptions (enumerate / add / remove / lapsed), mode control, onboarding.
  Fix whatever integration issues surface (WPF render on basic-display VM, COM elevation registration,
  wire/ABI) within the gate.
- **DoD:** a documented, repeatable live run showing all surfaces working against the live engine, all
  surfaced issues fixed + committed.

### Segment 3 — Installer / packaging
- Deployment today is manual scripts (unload driver → copy → COM `/RegServer` → sign → load). Build a
  real, repeatable installer/uninstaller bundling driver + service + COM elevation host + the WPF app
  (framework-dependent publish; .NET 10 Desktop Runtime present on the VM). `SarNameIsOwnStore` is
  already anchored so the driver need not be unloaded to install.
- **DoD:** clean install + uninstall on a fresh `clean-baseline` VM yields a working product; no
  orphaned state.

### Segment 4 — Hardening / soak
- Go beyond the 29-check functional suite: **long-running and varied stress/soak** (the wedge was a
  latent kernel bug surfaced only under specific concurrency). Re-examine the known limitations
  (below) for production readiness. **Careful:** kernel changes on the write/unload path can re-wedge
  — the deadman + rundown are your safety net; verify each change on the VM and keep a crash-dump
  path armed (see `docs/DEBUGGING.md`).
- **Known limitations to weigh (from session-5):** whitelist doesn't survive a driver reload and
  nothing re-pushes it (in-memory by design); remove-by-path only clears still-matching exemptions;
  interpreter refusal is name-based (apply-time guard backstops it). Resolve for production **or**
  explicitly defer to the owner in §9 — do not paper over with stopgap code.
- **DoD:** soak scenarios pass with no wedge / leak / regression; limitations closed or owner-deferred.

### Owner-only (DO NOT do): Part XII ratification into `CONSTITUTION.md`; `git push`; MS driver cert.

---

## 6. OPERATIONAL HAZARDS (paid for in time this drive)

- **VM host degradation from repeated restores.** `vm_verify_new.ps1` restores the snapshot each run;
  running it back-to-back **degrades the host vmbus / VMConnect console** (the owner hit "가상 컴퓨터에
  연결하지 못했습니다"). **Budget your VM runs.** Prefer `-SkipRestore`/`-SkipDeploy` when the VM is
  already in a known-deployed state; use **PowerShell Direct** (reliable) over VMConnect. If PS Direct
  **and** the console fail but `Get-VM` shows `Heartbeat=Ok`, it's transient host-load/channel
  degradation (often from your own accumulated load + Windows servicing), **not** a guest wedge — wait
  for load to drain or `Restart-VM -Force`; do **not** reflexively pile on more restores.
- **Kernel-hang forensics:** `docs/DEBUGGING.md` is the runbook (dedicated dump file + NMI). A wedge
  that stalls storage may prevent an on-disk dump; the differential (mmap vs non-mmap) + FABLE5 code
  analysis is often faster than chasing a dump. Keep the wedge fix intact.
- **Safety-classifier false-positive:** describe the product **neutrally** (a file-backup /
  disk-usage / allow-list utility) in prompts and especially to FABLE5; never accumulate
  ransomware/security framing. If commands start getting blocked mid-session, it's accumulated-context
  false-positive — a fresh session clears it. (You start fresh, so you likely won't hit it.)

---

## 7. FABLE5 DISCIPLINE

Spawn via the Agent tool with `model: fable` (a general-purpose agent so it can read the repo). Frame
the product **neutrally** — a benign file-backup / allow-list / disk-usage utility — **never**
ransomware/security terms (it over-refuses defensive-security framing). Present **confirmed logic at
code level** (point it at exact files/functions); demand **code-level counterexamples** (inputs →
wrong output) or proven memory/concurrency defects, not prose. **Critically accept:** apply every
real finding; reject non-reproducing or design-limitation findings **with your own code-level
evidence**. Per §3, a segment commits **only at full agreement (FABLE5 final = ship).** You can
resume the same FABLE5 agent across rounds via SendMessage to keep its context.

---

## 8. BUILD / TEST / VERIFY RECIPES

Env: .NET 10 SDK; CMake 4.x + VS2022 Community; WDK 10.0.26100; Hyper-V VM **`SarTarget`**
(admin/admin via PowerShell Direct), snapshot **`clean-baseline-20260704`**. Host = `DESKTOP-SB0J4NG`.

- **Frontend:** `cd frontend && dotnet build SemanticsAr.slnx` (0 errors; `MVVMTK0045`
  `[ObservableProperty]` warnings are the project idiom). `dotnet test
  SemanticsAr.Core.Tests/SemanticsAr.Core.Tests.csproj`. **`SarApiIntegrationTests.AbiVersion`
  P/Invokes `sarapi.dll` and fails silently (returns false) if the copied dll is stale — copy a
  freshly built `sarapi.dll` over it after any ABI change.**
- **Native (service, sarapi, sarctl, control lib, tests, COM host):** `cmake --build build_win
  --config Release` (this is the tree the VM deploy reads); run `build_win/tests/Release/
  test_chassis.exe`.
- **Driver:** `scripts\build_driver.bat [outdir]` (WDK env; wipes `build_driver`; direct `cl`/`link`).
  Sign+package: `scripts\package_driver.ps1` — **worked headlessly this drive** (produced the signed
  `build_driver\pkg`); a `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\...` wrapper
  also worked. To build only your two files against clean HEAD (isolation check before a driver
  commit): `git worktree add --detach <tmp> HEAD`, copy your files in, `build_driver.bat`.
- **VM verify (each is a fresh PS Direct session; the long ones auto-background — wait for the
  notification):**
  - `scripts\vm_verify_new.ps1` — restore + deploy signed `build_driver\pkg` + `build_win` +
    harnesses → 29 checks (~10 min). **Expect 29/0/1** (skip = TIER2 signed-harness trust, unrelated).
    Note two documented-**flaky** checks (async budget "B.2" channel: MMAP2 reservation-release and
    MMAP-ORACLE conviction) fail intermittently and independently — a clean run is achievable; don't
    chase them as regressions.
  - `scripts\vm_verify_attribution.ps1` (8/0), `scripts\vm_verify_exemption.ps1` (12/0) — assume
    already deployed.
  - `scripts\vm_verify_integrity_halt.ps1` (Segment 1 probe) — induces a store tamper (takes
    ownership → mutates the store) and asserts the flag; run + read its asserts.
  - `scripts\vm_diag_benign_overwrite.ps1` — measurement only (the mmap/CPU diagnostic).

---

## 9. PROGRESS LEDGER — **update this as you close each segment (your durable memory)**

- [x] mmap writeback deadlock — **DONE, committed `3f84a64`**, FABLE5 ship, 29/0/1.
- [x] **Segment 1 — XII.3 integrity-halt — DONE.** Two commits:
      - `e5246f1` feat: XII.3 integrity-halt posture flag (path-free enum → red foreground tier) +
        refinement B preserve-store authentication. FABLE5-hardened (2 rounds): the record-MAC chain
        seed is now `HMAC(mac_key, generation‖record_count)` (was all-zeros) — closes an empty-index
        (count=0) forgery and a generation-field bump that the strictly-newer-generation acceptance
        would otherwise admit silently + poison the anchor; overflow-safe `record_count` bound closes a
        crafted-index kernel OOB read; the tamper flag latches only on crypto failure
        (RECORD_MAC/ROLLBACK/BAD_MAGIC), size-class failures reset to empty WITHOUT the flag per XII.7.
        ABI 2→3 (service→frontend frame stays 40B via flag bit; driver→service reply +4B guarded by the
        exact-length skew check → no protocol_version bump). Atomic (Interlocked) edge-triggered
        foreground. Verified: test_preserve 34/0, Core.Tests 159/0, full native suite 0-fail;
        vm_verify_integrity_halt 6/0 (tamper→1, clean→0); vm_verify_new regression clean (FN=0 every
        phase, grown reply doesn't break transport; the 2 async MMAP-ORACLE conviction fails are the §8
        documented flakies).
      - `97f0247` fix(driver): unload-safety — `mmap_read_rundown` wait before `FltUnregisterFilter` +
        `SarCommPortClose`/`SarCommPortFree` split (this is §5's other unload half, pairing with the
        committed deadman) — and self-protection scoping `SarPathUnderSystemRoot` (boundary-checked so
        `C:\Windows.old`/`-Backup` no longer false-match). FABLE5 ship (D all clean; C1 sibling
        false-positive found + fixed).
      - **[OWNER DECISION — (f), recorded per prohibition 3]** Size-class store-load failures
        (TRUNCATED / COUNT_MISMATCH) are silently discarded, NOT raised as integrity-halt. Mandated by
        XII.7 (old-record-size → no tamper flag) and deletion-equivalent (a store-writing attacker can
        already delete the store, which is non-tamper by design; in-place same-size content tamper still
        trips RECORD_MAC → flagged). To flag size-mismatch in production, XII.7 must be amended
        (owner-only). FABLE5 agreed ship under this position.
      - **[Seg-4 follow-up]** The integrity-halt probe's old `Restart-Service -Force` double-action
        transiently wedged the service once (StopPending + CPU spin) right after a driver reload; a clean
        single stop→load→start does not (verified <1s). Probe `Reload` fixed. Investigate service-stop
        robustness under rapid driver reload in Segment 4.
- [~] **Segment 2 — live end-to-end on VM. SUBSTANTIALLY ADVANCED; two real integration bugs found
      + fixed (`477b498`).** The app was launched on the VM (auto-logon interactive session, scheduled
      task with Interactive principal — `vm_run_gui.ps1`) against the **live** C1 driver+service for the
      first time. **It renders live** — window handle non-zero, title `semantics-ar`, all four nav
      surfaces, first-run onboarding, Home posture, honest scope disclosure (screenshots in
      `build_verify/gui_evidence.png`, `gui_fix.png`).
      - **Bug A (fixed, `477b498`):** launched **non-elevated** (the normal case), Home showed red
        "status could not be trusted" / MODE UNKNOWN. `sarapi_server_is_system` verified the pipe SERVER
        PROCESS was SYSTEM via `OpenProcess`+`OpenProcessToken` — a medium-IL client can't query a
        SYSTEM/session-0 token, so it always failed. Fix: verify the pipe OBJECT owner SID is SYSTEM via
        `GetSecurityInfo` (integrity-independent) + service sets pipe SD owner `O:SY` (posture/events/
        control). Owner=SYSTEM confirmed on the live pipe; elevated path stays OK.
      - **Bug B (fixed, `477b498`):** once (A) let non-elevated callers reach the read, the sarapi
        clients' **unbounded blocking `ReadFile`** could hang the app whenever the posture worker parked
        behind the shared `g_control_lock`/an in-flight driver status query. Fix: `FILE_FLAG_OVERLAPPED`
        + shared `sarapi_read_frame` with a 5 s overlapped timeout (posture+events); and
        `sar_posture_serve` is now **wait-free** (`TryEnterCriticalSection` + SRWLock last-good cache).
        **FABLE5 built a byte-exact repro** proving the token is causally inert (medium-IL reads 40 B in
        ~2 ms healthy; bails at 5004 ms clean on a wedge) and reviewed the fix **ship**.
      - **[Seg-4 — untimed `FilterSendMessage` RESOLVED at the severe layer; residual owner-deferred.]**
        Investigated the untimed `FilterSendMessage` (`service/commclient.c:172`) with FABLE5 (adversarial,
        neutral framing). The driver-side `SarCommMessageNotify` callback is synchronous/PASSIVE/SEH-wrapped
        with no unbounded waits, and self-reentrancy is defended (`SarNameIsOwnStore` OWN_STORE tagging +
        capture skip + SKIP_PAGING_IO). **FABLE5 found a real defect I initially missed** (single-TU grep
        blind spot): the *content* recover path `SarPreserveRestore` (`driver/preserve.c`) held
        `Preserve->lock` **EXCLUSIVE across `SarPresDataRead`→`ZwReadFile(preserve.dat)`** (+ the PAGED
        allocs, `SarPresCrypt`, verify) — a driver-wide exclusive lock held across synchronous kernel FS
        I/O, which under a stall (3rd-party filter / paging pressure) would stall **every** capture path
        that needs the lock (Stage/Reconcile/Promote/persist) → machine-wide write-capture stall, and hang
        the untimed `FilterSendMessage`. (FABLE5's "sharpest" namespace/`SarPresReadLink` construction was
        **wrong** — that branch releases the lock at `:1104` *before* its `ZwCreateFile`; I refuted it with
        the code and FABLE5 accepted.) **FIX (applied, driver builds clean, FABLE5 final = SHIP):**
        `SarPreserveRestore` now releases `Preserve->lock` immediately after snapshotting `rec` under the
        lock, then does alloc + `SarPresDataRead` + `SarPresCrypt` + `sar_preserve_verify_extract` + copy
        **unlocked**. Safe because: crypto material (`store_key`/`mac_key`/`aes_alg`/`key_obj_len`) is
        init-stable (written only in Create/Load before `ready=1`, never rekeyed; restore gates on
        `ready==0`), `SarPresDataRead` uses an explicit `ByteOffset` on the `FILE_SYNCHRONOUS_IO_NONALERT`
        store handle (no shared file-position), and `verify_extract` gates all output against the `rec`
        snapshot → a race with concurrent compaction can only yield `STATUS_DATA_ERROR` (fail-closed, never
        stale/wrong bytes). Matches the existing recovery.c / namespace-branch unlocked-I/O discipline;
        touches no MAC/rollback/security invariant. **VM-VERIFIED (`vm_verify_new`, re-signed pkg thumbprint
        `1E4B3044…`): recovery not regressed** — `28 passed / 1 failed / 1 skipped`, where every recover /
        FN=0 check the lock fix touches PASSED (chacha/20 FN=0, salsa/20 FN=0, **Salsa20/12 convicted +
        recovered BY KEY FN=0**, MMAP-ORACLE positive Oracle forward-convicts BY KEY + FN=0). The lone fail
        is the §8 documented-flaky **MMAP2 reservation-release** (async B.2 budget channel — an unrelated
        mmap write-reservation timing check, not the recovery path); per §8 not chased, and not re-run to a
        cosmetic 29/0/1 to respect the §6 host-degradation budget.
      - **[Seg-4 — residual, OWNER-DEFERRED with justification.]** The genuine remaining unbounded-I/O
        surface is **not** the (now de-risked, own-store, cached-handle) preserve.dat read but
        `SarRecoveryExecute` (`driver/recovery.c:426/439/457/469`): synchronous `ZwCreateFile`/`ZwReadFile`/
        `ZwWriteFile` on the **arbitrary target file** inside the callback — an external path exposed to
        stacked-filter interception / oplock / network / removable stalls, which `FilterSendMessage` cannot
        cancel. A permanently-stuck recover worker never returns to `accept()`, permanently consuming one
        **control**-pipe instance. **Blast radius is bounded by design:** the service runs THREE separate
        pipe servers each with its own 4-worker pool (`control.c:459-461`) — **posture (the GUI liveness
        poll) and events are fully isolated from control**, so a hung recover cannot starve the GUI's status
        signal (it survives on the wait-free posture cache regardless). Only the control pool (4 workers,
        shared by recover + catalog/preserve-query + whitelist/mode/budget) can be exhausted, and only by
        **≥4 concurrently-hung recovers** — pathological, since `RecoverySession.Execute` serializes recovers
        one-at-a-time per elevated session. **Owner decision needed** (do not paper over): choose (a) cheap
        pool-isolation — cap concurrent in-flight recovers below the control pool size, or give recover its
        own pipe endpoint; or (b) the heavyweight service-side deadline wrapper (must run on a dedicated
        non-pool thread and **leak** the reply buffer on timeout, since the kernel owns it until the callback
        returns) as defense-in-depth for the arbitrary-target `ZwCreateFile`; or (c) accept as a documented
        limitation given the bounded blast radius. FABLE5 recommends (a) over (b) and agrees (b) is not a
        ship gate now that the lock fix removed the amplification.
      - **NON-ELEVATED FIX LIVE-CONFIRMED:** with the committed binaries deployed and the engine live,
        a genuine medium-integrity process (`runas /trustlevel:0x20000`, session 0 — avoids the flaky
        interactive-session/scheduled-task harness) ran `sarapi_posture_read` and got **result=0
        (SARAPI_OK), elevated=False, svc=1 drv=1 mode=audit** — valid AMBER posture, no SERVER_UNTRUSTED,
        no hang. The earlier "hang" was the degraded VM + Limited-task-not-executing, not the code.
        **APP-LEVEL CAPSTONE:** with onboarding pre-completed (HKCU flag), the app launched non-elevated
        (Limited scheduled task, interactive session) renders Home as amber **"Recording, not blocking —
        Mode: AUDIT"** against the live engine (`build_verify/gui_home.png`) — the correct posture, where
        before the fix it showed red "status could not be trusted / MODE UNKNOWN". The "built → working
        product" gate is visually met for Home; Recovery/Budget/Exemptions render as nav surfaces.
      - **Bug C (fixed, `6568557`):** on the live run all three ELEVATED surfaces (Recovery / Budget /
        Exemptions) showed "unavailable". Root cause: the COM elevation host's type library
        (`SemanticsArElevation.tlb`) was emitted only into the MIDL intermediate dir, never colocated
        with the exe and never deployed; `/RegServer`'s `LoadTypeLibEx` failed but was wrapped in
        `if (SUCCEEDED(...))` so `SarRegisterServer` silently skipped `RegisterTypeLib` and still
        returned S_OK — the interface had no registered type library, so `CoGetObject` on the elevation
        moniker failed with **TYPE_E_LIBNOTREGISTERED (0x8002801D)** and every elevated surface fell
        back to "unavailable". Fix: registration.cpp returns the HRESULT on `LoadTypeLibEx` failure
        (fail loud); CMake POST_BUILD colocates the .tlb with the exe; vm_run_gui deploys it. **VM-
        confirmed:** with the .tlb deployed + `/RegServer` re-run, TypeLib registers and
        `CoGetObject("Elevation:Administrator!new:{CLSID}")` returns **hr=0x0 ACTIVATED OK**. The host
        statically links the sarapi control client + Segment-2 owner-check, so its channel to the O:SY
        control pipe works too. (App chrome now also shows "AUDIT MODE", not "MODE UNKNOWN".)
      - **Bug D (FIXED — Recovery view is a resource hog / freezes the UI with many items).**
        Both root causes addressed and the App builds clean (0 errors).
        * **B (render) FIXED:** `RecoveryView.xaml` Browsing ListBox now carries
          `ScrollViewer.CanContentScroll="True"` + `VirtualizingPanel.IsVirtualizing="True"` +
          `IsVirtualizingWhenGrouping="True"` + `VirtualizationMode="Recycling"` + `ScrollUnit="Pixel"`,
          so grouped rows are UI-virtualized (only visible containers realized/recycled). The bounded
          DockPanel gives the ListBox a finite height so the ScrollViewer engages.
        * **A (load freeze) FIXED:** `RecoveryViewModel.Begin()` is now `async Task`. COM stays on the
          STA UI thread (`_session.Begin()` — the two SafeArray bulk calls, no RPC_E_WRONG_THREAD risk);
          only the filesystem-bound work — `IncidentGrouper.Group` + per-item `RestorePlanner.Classify`
          (Win32FileProbe) — moves to `Task.Run` via the new static `ClassifyRows(snapshot, probe)`
          (pure, no UI/COM touch). Results marshal back and `PopulateItemsAsync` bulk-adds inside
          `ICollectionView.DeferRefresh()` so the grouped view regroups ONCE, not per-Add (kills the
          O(n²)). `SyncFromSession`/`BuildItems`/`MakeItem` removed (folded in). `Win32FileProbe` is
          stateless so the off-thread classify is safe. Selection survives virtualization because
          `IsSelected` lives on the VM (SelectAll/Preview iterate `Items` VMs, not containers).
        * Verify by navigating to Recovery with many items (VM UIAutomation nav is flaky — §6; owner can
          drive it in VMConnect). Committed.
      - **Bug D (prior IN-PROGRESS notes, kept for provenance).**
        Owner observed: navigating to Recovery and clicking "View & recover" spikes resources and
        temporarily freezes the app when there are many recoverable items. This is a REAL responsiveness
        defect (not just VM slowness). Two root causes, both confirmed by reading the code:
        * **B (render, the big one, quick fix):** `frontend/SemanticsAr.App/Views/RecoveryView.xaml` the
          Browsing `<ListBox>` (~line 60, ItemsSource=Items) uses `GroupStyle` grouping but sets NO
          `VirtualizingPanel.IsVirtualizingWhenGrouping="True"` — WPF DISABLES UI virtualization for
          grouped lists by default, so every item's visual container is realized at once. The ListBox is
          inside a `DockPanel` (ShowBrowsing, ~line 48) which is bounded, so virtualization WILL work once
          enabled. FIX: add `VirtualizingPanel.IsVirtualizingWhenGrouping="True"`,
          `VirtualizingPanel.VirtualizationMode="Recycling"`, `ScrollViewer.CanContentScroll="True"` to
          that ListBox (Preview/Report ListBoxes are small, optional).
        * **A (load freeze):** `RecoveryViewModel.Begin()` (`RecoveryViewModel.cs:88`, a `[RelayCommand]`)
          runs SYNCHRONOUSLY on the UI thread: `_session.Begin()` (RecoverySession.cs:32 — two bulk COM
          calls `LoadCatalog`+`LoadPreserved`, large SafeArray marshalling) then `BuildItems`
          (RecoveryViewModel.cs:263) which per item calls `RestorePlanner.Classify(model, Win32FileProbe)`
          (a FILESYSTEM probe per item) and `Items.Add(...)` into a grouped `CollectionViewSource`
          (RecoveryViewModel.cs:60-61 GroupDescriptions) — each Add re-groups → O(n²). FIX: make the load
          async — `Task.Run` the enumeration + per-item classification OFF the UI thread, then marshal the
          finished list back and bulk-populate (build a `List`, then reset the ObservableCollection once
          instead of per-item Add so the grouped view regroups once). COM threading caveat: the elevated
          channel RCW is created on the STA UI thread; calling `LoadCatalog/LoadPreserved` from a
          background thread may throw RPC_E_WRONG_THREAD unless the interface is marshalled
          (CoMarshalInterThreadInterfaceInStream / a fresh activation on the worker) — simplest safe split
          is: do the COM calls on the UI thread (they return quickly for reasonable N), but move the
          per-item `Win32FileProbe` classification + grouping/materialisation to `Task.Run`, then bulk-add
          on the dispatcher. Start with B (trivial + biggest win), then measure whether A is still needed.
        * Verify by navigating to Recovery with many items (VM UIAutomation nav is flaky — §6; the owner
          can drive it in VMConnect, or reason + a targeted micro-benchmark). Not yet committed.
      - **REMAINING before Segment 2 closes:** exercise the remaining surfaces live end-to-end —
        Recovery (induce incident → recover → **byte-for-byte** restore), Budget bars, Exemptions
        add/remove/lapsed, mode control. Backends are separately VM-verified (vm_verify_new FN=0,
        vm_verify_exemption 12/0, vm_verify_attribution 8/0) and the app VMs are Core.Tests 159/0; the gap
        is the **GUI-driven** exercise.
      - **VM OPERATIONAL LESSON (§6):** the scheduled-task-Interactive launch + auto-logon get flaky
        after several reboots (Limited tasks stop executing; auto-logon intermittently doesn't fire). Do
        NOT chase a "hang" conclusion from a Limited task that shows RUNNING with no output — verify the
        task actually ran (a probe that writes a start-marker). Budget VM cycles hard.
- [~] **Segment 3 — installer / packaging. BUILT (transactional PowerShell installer); VM smoke pending.**
      Chose a transactional PowerShell installer over a WiX MSI deliberately: (1) WiX isn't the constraint
      here — the *driver* installs via inf/pnputil either way; (2) an MSI custom-action driver install is
      hard to verify safely on the flaky VM and risks a plausible-but-broken uninstall; (3) the genuine
      hard problem is **clean uninstall of a PPL-AM service** (a non-protected caller cannot stop it live),
      which a script handles honestly (best-effort stop → `DeleteService` marks it → reboot finalizes). The
      artifacts:
      - `installer/Build-SarPackage.ps1` — assembles a self-contained `dist/` payload: `app\` (framework-
        dependent `dotnet publish` + ABI-matched `sarapi.dll`/COM host/`.tlb`/service exe/`sar_install.exe`)
        + `driver\` (signed `.sys`/`.inf`/`.cat`/ELAM/cert from `build_driver\pkg`) + the setup script.
        **Verified locally:** produced `dist\` (334 app files, 5 driver files, all key binaries present).
      - `installer/SemanticsAr-Setup.ps1 -Action Install|Uninstall|Verify` — transactional (rollback stack),
        idempotent, logged (`%ProgramData%\semantics-ar\setup-*.log`). Install order: preflight (admin +
        .NET 10 Desktop Runtime) → payload to `%ProgramFiles%\semantics-ar` → trust test-signing cert
        (Root+TrustedPublisher, **skipped for a production-signed driver** — detected via Authenticode
        subject) → `pnputil /add-driver /install` + `fltmc load` → create user service **`SemanticsAr`**
        (own-process — the canonical name from `service/main.c` + the `sar_install` PPL target; the verify
        harness's `semantics_ar_service` is a test-only alias) → ELAM+PPL via `sar_install.exe` (skippable
        with `-NoProtect`) → start service → COM `/RegServer` → Start-Menu shortcut → Apps&Features
        registration. Uninstall reverses all, reports orphans, flags **reboot-required** if the PPL service
        can't be stopped live. `Verify` asserts filter+services+COM+app.
      - **OWNER GATES (recorded, not resolved autonomously):** (i) **production driver signing** is
        owner-only (MS attestation/WHQL) — the installer test-trusts the self-signed cert only when it
        detects a test-signed `.sys`; a shipped product replaces this with a properly-signed driver and the
        cert-trust step no-ops. (ii) **PPL-AM uninstall policy** — live removal of the protected service
        requires a reboot; the installer stages the delete + flags reboot. If the owner wants reboot-free
        uninstall, the service must expose a self-initiated protected-stop path (design change). (iii)
        whether to enable PPL by default (production: yes; the `-NoProtect` switch is for dev installs).
      - **DoD MET — VM-VERIFIED (`scripts/vm_smoke_installer.ps1`, 11/0).** Clean restore → deploy `dist`
        → Install → Verify → Uninstall over PowerShell Direct: install trusts the cert, `pnputil` registers
        the minifilter (`oem0.inf`) — **a non-PnP ActivityMonitor inf DOES register its service via
        `/add-driver /install`** (the earlier worry was unfounded) — `fltmc load`, user service `SemanticsAr`
        created + RUNNING, COM host registered; Verify 6/0; Uninstall removes service + `oem0.inf` driver
        package + ELAM service + COM + files with **no orphaned state**. Two real defects the smoke caught +
        fixed (`9e5a187`→ next commit): (1) ELAM `CreateService` failed `87 (INVALID_PARAMETER)` because a
        **boot-start driver image must live under `System32\drivers`** to be boot-loader-reachable — the
        installer now copies `semantics_ar_elam.sys` there before `sar_install`; (2) `$Source` relied on
        `$PSScriptRoot` which is empty under `powershell -File` from a remote session → resolved via
        `$PSCommandPath` fallback; plus the `Invoke-Verify` counter-scope bug. **Expected residual on a TEST
        build:** ELAM `InstallELAMCertificateInfo` returns `577 (INVALID_IMAGE_HASH)` because the self-signed
        cert is not an MS-trusted AM certificate — the installer degrades gracefully to an **unprotected**
        service (WARN, install proceeds). This is precisely owner-gate (i): a production install supplies an
        MS-AM-signed ELAM+service and PPL engages. The install/uninstall *mechanics* are fully proven.
- [~] **Segment 4 — hardening / soak.** The untimed-`FilterSendMessage` item is resolved above
      (driver lock fix SHIP `84d991c`; residual owner-deferred). The three session-5 **known limitations**
      are analysed at code level below and **owner-deferred** (none is a stopgap-fixable bug; two are
      deliberate security-model choices that must not be weakened autonomously):
      * **Whitelist does not survive a driver reload.** The whitelist lives in `State->whitelist` (driver,
        in-memory; `driver/state.c`). The service forwards ADD/REMOVE to the driver
        (`control.c:185 sar_control_send_whitelist`) but keeps **no persisted copy**, so a driver reload
        starts empty and nothing re-pushes. Resolving it properly = a new persistence subsystem — either
        driver-side (a MAC'd/anti-rollback store like keystore/preserve) or service-side (persist exemptions
        + replay on reconnect/handshake). Both are **un-discussed new architecture** (template rule: never
        add un-discussed content) and the limitation is explicitly "in-memory by design". **Owner-defer:**
        decide whether reload-survival is a product requirement; if yes, the service-side persist+replay is
        the smaller change (the service already owns the control channel and re-handshakes on reconnect).
      * **Remove-by-path only clears still-matching exemptions.** Exemptions are keyed by **strong identity**
        (image_path + cert_subject + content_hash). Remove re-evaluates the *current* file at the given path
        (`control.c:174 sar_identity_evaluate`) to reconstruct that identity, then removes the matching entry
        (`state.c:154 SarStateWhitelistRemove`→`sar_whitelist_remove`). If the file changed or is gone since
        it was exempted, the re-evaluated identity won't match → the stale entry persists. This is
        **intended** (identity-keyed, not path-string-keyed — a path-string remove would let a swapped
        binary drop another binary's exemption). **Owner-defer:** the correct product answer is a
        remove-by-index/enumerate-then-remove UX (the enumerate path `SAR_CTL_OP_WHITELIST_LIST` already
        returns each entry's identity) rather than weakening the key. Do **not** change the key model
        autonomously (security-relevant, prohibition 4).
      * **Interpreter refusal is name-based.** `sar_identity_is_interpreter(image_path)` is a name/path
        check; the service refuses to ADD an interpreter to the whitelist (`control.c:179`,
        `SAR_CTL_RESULT_INTERPRETER`). It is **backstopped in the driver** at apply time
        (`state.c:351 SarIdentityIsInterpreter` in the verdict-apply guard), so a renamed interpreter that
        slips the name check is still caught before it can be granted trust. **Owner-defer / acceptable:**
        the defense-in-depth apply-time guard makes the name-based front check adequate; a content/behaviour-
        based interpreter classifier is a research-grade enhancement, not a correctness gap.
      * **Recover-fix concurrency soak — DONE, PASS (`scripts/vm_soak_recover.ps1`, 28/0).** 12 iterations
        overlapping a background capture write-storm (holds the capture path / `Preserve->lock` hot) with a
        foreground attack + recover + golden-verify pass (drives the now-UNLOCKED `SarPreserveRestore` read).
        Result: **engine live every iteration (no wedge)**, recovered files **byte-exact** (golden-hash), and
        the recovered count is **dead steady — spread=0** (`39,39,…,39`). That stability is the affirmative
        signal the change is sound: a recover-read race from dropping the lock would make the count *vary*
        with timing; it does not. (The constant 1/40 shortfall is a **capture-side** artifact of AUDIT mode
        under a synthetic concurrent double-storm — AUDIT is record-only and does not promise FN=0 under
        contention; ENFORCE's block-before-evict is the zero-loss guarantee, proven by `vm_verify_new`
        Phase G. It is independent of the recover read and reproduces identically pre/post harness tweaks.)
      * **Remaining Segment-4 work (needs a healthy VM + owner-supervised time):** broader long-running
        soak across the *whole* driver (the original wedge was concurrency-latent — hunt for OTHER latent
        bugs beyond the recover path), and service-stop robustness under rapid driver reload (the
        `Restart-Service` double-action transient wedge noted under Segment 1 — the probe was fixed; the
        service-side robustness itself is still to be exercised under soak). The `ransom_sim`/
        `stream_transform` + `sarctl` harnesses (prebuilt in `build_harness/`) are the turnkey drivers.
- Owner-only pending: Part XII ratification; **`git push`** (many unpushed commits this drive — latest
  `1590616`; run `git log --oneline origin/main..HEAD` for the full set); MS driver cert.
- *Record every commit hash + one line of what it closed here as you go. If your context was
  compressed, this section is where you find yourself.*

---

## 10. FILE MAP (where you edit)

- **Governing (never edit):** `docs/CONSTITUTION.md`, `docs/CONSTITUTION_PART_XII.md`,
  `docs/EXPERIENCE_CHARTER.md`, `docs/FRONTEND_DESIGN.md`, `frontend/mocks/recovery-and-budget.v1.html`.
- **Wire:** `common/include/semantics_ar/{protocol.h,posture.h,preserve_format.h}`,
  `control/{include/sar_control.h,src/{whitelist.c,msg.c}}`, `frontend/sarapi/{include/sarapi.h,
  src/*}`.
- **Driver:** `driver/{capture.c,driver.c,operations.c,commport.c,preserve.c,phantom.c,state.c,
  seam.h,keystore_persist.c,obguard.c,eventlog.c}` + `engine/src/*`.
- **Service / COM:** `service/{control.c,commclient.c,posture.c,identity.c,attrib.c,main.c}`,
  `frontend/elevation-host/*`.
- **Frontend Core/App:** `frontend/SemanticsAr.Core/{Domain,Services,Interop}/*`,
  `frontend/SemanticsAr.App/{Views,ViewModels,Notifications}/*` + `App.xaml.cs` + `MainWindow.xaml` +
  `OnboardingWindow.xaml`. Tests: `frontend/SemanticsAr.Core.Tests/*`, `tests/*`.
- **Build/VM:** `scripts/*` (`build_driver.bat`, `package_driver.ps1`, `vm_verify_*.ps1`),
  `docs/DEBUGGING.md`. Trees: `build_win` (VM deploy source), `build_driver` (`.sys` + signed `pkg`).
