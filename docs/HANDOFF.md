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
      - **[Seg-4 follow-up]** The only remaining infinite wait is the **untimed `FilterSendMessage`**
        (`service/commclient.c:172`) into the driver status query — now insulated, hunt with a service
        dump next wedge (the preserve-lock family is the suspect).
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
      - **REMAINING before Segment 2 closes:** exercise the remaining surfaces live end-to-end —
        Recovery (induce incident → recover → **byte-for-byte** restore), Budget bars, Exemptions
        add/remove/lapsed, mode control. Backends are separately VM-verified (vm_verify_new FN=0,
        vm_verify_exemption 12/0, vm_verify_attribution 8/0) and the app VMs are Core.Tests 159/0; the gap
        is the **GUI-driven** exercise.
      - **VM OPERATIONAL LESSON (§6):** the scheduled-task-Interactive launch + auto-logon get flaky
        after several reboots (Limited tasks stop executing; auto-logon intermittently doesn't fire). Do
        NOT chase a "hang" conclusion from a Limited task that shows RUNNING with no output — verify the
        task actually ran (a probe that writes a start-marker). Budget VM cycles hard.
- [ ] Segment 3 — installer / packaging. *Not started.*
- [ ] Segment 4 — hardening / soak + known-limitation resolution + service-stop robustness + untimed
      FilterSendMessage (above).
- Owner-only pending: Part XII ratification; push (4 unpushed: `3f84a64`, `e5246f1`, `97f0247`,
  `477b498`); MS cert.
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
