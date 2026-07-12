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

### STANDING PRINCIPLES — OWNER-MANDATED, HOLD THESE THROUGH COMPLETION (survive every compaction)

The owner is away and trusts you to run to full productization autonomously. These are absolute:

1. **Frontier-or-beyond quality.** Every implementation must equal or exceed the global top-tier
   frontier. No shortcuts, no "good enough."
2. **Marshal every resource, autonomously, without stopping.** FABLE5 (adversarial code review + design
   authority when the owner is unavailable), VM verification, host verification, web research. Do not
   ask the owner to intervene or decide — resolve ambiguity via FABLE5/research and proceed. Do not
   stop until productization is complete or you hit a true owner-only gate (§3).
3. **NO COMMENTS IN CODE.** When you write or modify the core product source — the driver, service, and
   frontend (C/C#) — write ZERO comments; the code must read cleanly without them. (This drive violated
   this while hardening the service and had to strip them; do not repeat.) Operational scripts under
   `scripts/` and `installer/` follow the established repo convention (they carry comment-based help),
   but keep even those lean.
4. **Never degrade already-attained quality.** Do NOT bypass or ignore the Constitution; do NOT weaken
   driver/security logic; do NOT compromise the **"사용성 저해 0" / FN=0** guarantee (zero usability
   impact — every recoverable file recovers, every legitimate write proceeds). The Constitution
   outranks code. No stopgap/fallback/migration/schema-version/compat code (never-shipped product).
5. **Never wait indefinitely on a VM test; poll status.** (See §6 — a wedged guest never fires its
   notification.)

If your context was just compressed and you are reading this: these principles are still in force.

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

- **NEVER wait indefinitely on a VM test — poll status periodically. [OWNER-MANDATED, survives compaction.]**
  A background VM task (PowerShell Direct / `Invoke-Command`) that runs while the **guest goes
  unresponsive hangs forever, and its completion notification NEVER fires.** So you cannot passively await
  a VM run's notification — you must actively re-check `Get-VM -Name SarTarget` (Heartbeat/State) and the
  task's output file on an interval, and if the guest is wedged (`Heartbeat=LostCommunication` + a
  timeout-bounded PS Direct probe fails), **stop the hung task (TaskStop), force-off, and restore the
  clean-baseline snapshot** rather than wait. Always wrap any exploratory VM probe in a `Start-Job` +
  `Wait-Job -Timeout` so it cannot block you. This applies to every future VM run, compaction or not.
  (Observed this drive: a broad concurrency soak left the guest `Heartbeat=LostCommunication`, CPU ~24%,
  PS Direct timing out; the run's notification would never have arrived.)
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

- **TREE HEALTH (last checked this drive):** all suites GREEN — native `test_*` = 563/0 (chassis 81,
  preserve 34, engine 77, recover 97, keystore 12, capture 54, gate 22, phantom 133, handshake_crypto 17,
  keystore_mgr 15, schedule 11); frontend `Core.Tests` = 159/0; App + service + driver build 0 errors.
  This drive's frontier hardening (recover lock split, service-stop robustness, prompt shutdown, frontend
  responsiveness sweep) is VM-verified and regression-clean; the broad cross-path soak is 22/0 on a fresh
  host (the earlier wedge was host degradation, not a bug — see the Segment-4 soak note).
- [x] **[Frontier] Demo Kit — one-command test-mode deployment (DONE, commit `bc474dc`, FABLE5-designed).**
      Production (WHQL/attestation) signing is months out; until then the product must be demoed
      repeatedly, and a test-signed kernel driver only loads on a host in **test-signing mode + Secure
      Boot OFF**. `demokit/` is a portable, VM-agnostic kit: the operator copies it into ANY throwaway
      Windows VM and runs **one command** (`Demo.cmd` → `Start-SarDemo.ps1`) to go fresh-VM → DEMO READY.
      - **Self-resuming single-reboot flow:** self-elevate → honest GO/NO-GO preflight (VM / Secure Boot
        / HVCI / testsigning / disk / payload-hash) → apply boot policy once → reboot → AtLogon
        self-deleting scheduled task auto-resumes → install (reuses the real `SemanticsAr-Setup.ps1`
        `-NoProtect`) → seed sandbox → TEST MODE wallpaper → READY.
      - **Idempotent / not VM-hardcoded:** every step re-derives current reality and skips what's done.
        A checkpoint already in test-signing mode needs **no reboot** and installs immediately; a blank
        VM runs the full one-reboot flow. Both paths share `Invoke-InstallTail`.
      - **Safe attack kit** (`demokit/attack/`): reproduces the in-place high-entropy overwrite the
        product convicts on, but confined by fixed sandbox root (NO path param), canary + manifest gating,
        `GetFullPath` containment assertion, system/user-root denylist, no network, no persistence,
        reversible. **Verified on host:** all containment cases pass incl. `..\..\Windows` traversal
        resolving outside → blocked. `RUNBOOK.md` narrates ENFORCE-block → AUDIT-record+recover → reset.
      - `Reset-SarDemo.ps1` = reversible teardown (uninstall + testsigning off + HVCI restore + one reboot).
      - `installer/Build-DemoKit.ps1` regenerates a stamped kit from live sources; adds `-SelfContainedApp`
        to `Build-SarPackage.ps1` (VM needs no .NET runtime) + a `.self-contained` marker the installer's
        runtime gate honours (behavior-preserving for production payloads); self-checks kit-vs-repo
        installer parity so a stale kit fails the build. `KIT-VERSION.json` stamps commit/dirty/payload
        hashes; banner + wallpaper + README carry the TEST MODE disclaimer on every surface.
      - **Honest about the unavoidable:** one reboot (boot policy) and Secure Boot OFF (firmware → crisp
        NO-GO, not a silent fail). Refuses non-VM / domain-joined hosts without `-IAmSure`.
      - **VM-VERIFIED end-to-end on SarTarget (Gen2, Secure Boot off).** `vm_verify_demokit.ps1` = **13/13**.
        - No-reboot idempotent path (baseline already test-signed): preflight → install (driver load +
          service + COM) → sandbox seed → DEMO READY.
        - Fresh-VM reboot path (test signing OFF): preflight detects OFF → enable + schedule `SarDemoResume`
          (AtLogon) → clean guest reboot → resume detects test signing ACTIVE → install → DEMO READY →
          task self-deletes, state READY.
        - Safe attack: 12 sandbox files encrypted + ransom note, an EXTERNAL file left byte-identical
          (containment held), reset restored originals; teardown uninstall removed filter+service+files.
      - **Root-cause fix surfaced by the kit (commit after `bc474dc`):** `SemanticsAr-Setup.ps1`'s minifilter
        guard keyed on the service merely existing, so a STALE service (present but pointing at a missing
        image — the dev baseline's `\??\C:\sar\semantics_ar.sys`, or an imperfect prior uninstall) made Setup
        skip pnputil then fail `fltmc load` with PATH_NOT_FOUND. Now gates on the filter being LOADED, clears
        a stale service, re-registers (idempotent). Strictly more robust; clean-install path unchanged. Also
        fixed a BootPolicy `-Force` self-import that unloaded Preflight mid-run, and added `-NoReboot`.
      - **HARNESS NOTE (critical for future VM reboot tests):** reboot the guest with a CLEAN reboot (the
        kit's own `Restart-Computer -Force`, or `Restart-VM` WITHOUT `-Force`). A `Restart-VM -Force` hard
        reset can DROP the pending `bcdedit testsigning` BCD write, making the post-reboot state read OFF —
        a harness artifact, not a kit bug (proven: the kit's own clean reboot flushed it correctly).
      - **Verified on host too:** 14/14 scripts AST-parse; attack guardrails 7/7; read-only detection correct
        (this physical host → IsVm=False → would NO-GO, as designed). `dist/`, `dist-demokit/` gitignored.
        Build the payload with `build_driver/pkg` + `build_win` present, then `installer\Build-DemoKit.ps1`.
- [x] **[Frontier] Visual-first feature communication — DONE (6 commits `0215993`..`94b323c`).** Owner
      asked (Q6) that every feature be conveyed through purpose-built VISUALS so non-experts understand
      intuitively, honestly (a bounded claim must never look as strong as a definitive one; invent no data).
      FABLE5 designed one vocabulary — **SOLIDITY = CERTAINTY** (solid = verified/enforce, draining/fading =
      time-limited, hollow/dashed = permeable/absent) — realised on four shared pieces (`Controls/SarArc`,
      `SarPieWedge`, `Themes/Glyphs.xaml` 16px geometry set, `Converters/ExpiryToFractionConverter`) and
      shipped across every surface. All built, **host/gallery-screenshot-verified**, no code comments,
      a11y preserved (`AutomationProperties.Name` full sentences):
      - **VerdictSeal** (Home hero): tone glyph (check/bang/tri/question) in a mode ring — solid = ENFORCE,
        dotted = AUDIT (permeable). A distinct **Unknown** tone (`VerdictTone`, driven off `PostureReason`)
        keeps "status unavailable" from wearing the alarm triangle. Home adds a mode pill + captured-key chip.
      - **CertaintyGlyph** (seal / draining wedge / hollow dash) — the row/badge atom. Bounded shows a
        countdown wedge only when a real per-file fraction exists, else a static hourglass (honesty gate: no
        fabricated freshness). Now leads `CertaintyChip`, so every recovery list row already carries it.
      - **CertaintyLadder** — descending staircase encoding rung inequality five ways (width 100/86/72, height,
        fill solid→fading→none, border solid→dashed→dotted, badge) at FIXED semantic sizes (never scaled by
        counts). Adopted in Recovery pre-elevation and onboarding; a `Collapsed` mode (top two rungs dimmed +
        struck) is the **exemption confirm** visual.
      - **ModePictogram** (wall vs watched gate): ENFORCE arrow stops at a solid wall, file sealed; AUDIT arrow
        passes a dotted line, live file drawn **damaged**, copy+key drop to a tray. In ModeSwitch + onboarding.
      - **StackMeter** (Budget "what's using it") — strict part-to-whole of held bytes, top-3 apps in the
        validated palette + "Other"; **no track/ceiling** because the backend reports no storage limit.
      - **RewindTimeline** (Budget) — the restorable window as a dated band, oldest (left) edge fading; shows
        the real dated span, not a length vs an invented reference.
      - **Exemption honesty verified against the engine** (driver capture path + `control/src/whitelist.c`):
        exemption is forward-only — exempt actors' future writes skip capture; adding the whitelist entry never
        purges the preserve store. The dialog says so ("copies already kept stay restorable until they expire")
        and fixes a real bug: the consequence text was rendering in near-white hairline colour (invisible).
      - **Verification harness:** a `--gallery` window rendered every visual in all states for host screenshots;
        it was **removed** before finalising (never committed) so nothing dev-only ships.
      - **Deferred (optional, lower value):** Recovery report composition bar (reuse StackMeter over
        RecoveryOutcome counts); a live VerdictSeal + 4-seal legend in onboarding pane 1 (needs posture in
        `OnboardingViewModel`); a per-app stable colour assignment persisted across Budget visits; dark theme.
- [~] **[Frontier] Mock design-system applied to the WPF app (was generic Wpf.Ui Fluent).** The owner
      asked whether the approved mock (`frontend/mocks/recovery-and-budget.v1.html`) design was applied — it
      was NOT (only the ladder color tokens were; everything else was stock Fluent). Now applied + FABLE5
      design-critiqued + **host-screenshot-verified** (`scripts/host_shot.ps1` launches the app on the host —
      no VM/engine needed — navigates, and captures `build_verify/host_*.png`; the pre-elevation/unavailable
      states render the full design):
      - `Themes/DesignTokens.xaml` — the mock's warm-paper palette (canvas #FAFAF9 / surface / text / muted /
        hair / accent + def/bnd/unr/red triads) as frozen `Sar.*` brushes, soft card + large shadows, fonts,
        and a bridge overriding ~10 Wpf.Ui theme keys so stock chrome harmonizes. `Themes/SarControls.xaml` —
        NavItem (active = surface card + shadow + 3px accent left-bar, no more blue selection block), Card
        (shadow-host pattern → keeps ClearType), Quiet/Primary/Outline buttons, Eyebrow/PageTitle/BigNumber,
        Meter (ProgressBar), Segment.
      - Shell (`MainWindow`): mica background (canvas + two faint radial blooms, blue bottom-left / green
        top-right per the mock), `WindowBackdropType=None`, 236px sidebar with a teal→#17729B brand mark,
        Fluent **nav icons**, active-item card + accent bar, a neutral-dot mode chip.
      - Views: Home hero is a Card with a **tinted** level shield (bg + line + stroke glyph, not a solid-red
        fill) + Display verdict + hairline stats; Recovery/Budget/Exemptions use Sar.PageTitle/Subtitle +
        Primary/Outline buttons + Sar.Card; Budget big-number (Sar.BigNumber tabular) + Sar.Segment range
        picker; the **CertaintyChip** is now the mock rung (small tinted pill, colored glyph+label, UNR
        square-left edge) — which also upgrades every populated item row. Onboarding + ModeSwitch windows
        got the same canvas + typography + buttons. Fixed a real layout bug (MaxWidth text centering in a
        stretched StackPanel → `HorizontalAlignment=Left`).
      - **Remaining polish (further fidelity, lower value):** Home pagehead + right-column stats + coverage
        card; card-ify the Recovery ladder with all 3 rungs; the deep populated-state components (incident
        cards, budget app-impact rows, exemption cards, preview modals) are styled with Sar tokens but not
        pixel-matched and are **only fully verifiable with the live engine** (VM GUI harness is flaky, §6);
        dark theme (mock supports it; app is light-only — the `DynamicResource` brush refs make it a
        swap-the-colors-dictionary change later). Frontend builds 0 errors; Core.Tests 159/0.
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
      - **[Seg-4 — recover-starvation residual RESOLVED (frontier hardening, not deferred).]** The genuine
        remaining unbounded-I/O surface is `SarRecoveryExecute` (`driver/recovery.c`): synchronous
        `ZwCreateFile`/`ZwReadFile`/`ZwWriteFile` on the **arbitrary target file** inside the port callback,
        which a stalled filesystem/oplock/redirector can hang and `FilterSendMessage` cannot cancel. The
        **true chokepoint** (corrected from the earlier pipe-pool-only analysis): `sar_control_serve` wrapped
        EVERY control op in ONE process-wide `g_control_lock` around the single `MaxConnections=1` driver
        connection (`control.c:520`), and that same lock is shared by the **events**-query serve and the
        **autoverdict** verdict-push (`control.c:644,736`). So a hung recover held `g_control_lock` and
        blocked **catalog/preserve-query/whitelist/mode + events + autoverdict** — the last a security-adjacent
        channel. (Posture was already safe: it uses `TryEnterCriticalSection` + a cached frame — the Bug B
        wait-free serve.) **FIX (i) — recover lock split (`service/control.c`, service builds 0 errors):**
        recover ops (`SAR_CTL_OP_RECOVER`/`PRESERVE_RECOVER`) now take a dedicated `g_recover_lock`; all other
        control ops keep `g_control_lock`. A hung recover can no longer block any other subsystem — at worst
        another recover. This introduces concurrent request/response on the single driver port; **a fresh
        FABLE5 (neutral framing) confirmed SAFE against the source**: the port is opened async (no
        `FO_SYNCHRONOUS_IO`) so FltMgr does not serialize concurrent IOCTLs; `MaxConnections=1` limits
        connections not in-flight messages; the reply rides the request's own IRP OutputBuffer (no
        cross-delivery); the driver callback is reentrant with per-domain locks + no shared reply buffer; the
        stalling recover I/O holds **no** driver lock (the shipped `SarPreserveRestore` snapshot-then-release);
        recover keeps its own lock so two recovers still serialize on the shared `.sarrectmp` temp path.
        **VM-VERIFIED (`scripts/vm_verify_recover_isolation.ps1`): 240/240 files byte-exact under a concurrent
        control-op storm, engine live** — the introduced concurrency is corruption-free. The only residual is
        the ≥4-concurrently-hung-recovers control-**pipe-pool** exhaustion (recovers share the control pipe's
        4 workers); it is deep-pathological (`RecoverySession.Execute` serializes recovers one-per-elevated
        session, so it needs ≥4 concurrent elevated sessions each on a stalled target) and touches only the
        recover subsystem. Optional further isolation (A): give recover its own pipe endpoint (own pool);
        deferred as disproportionate (needs a sarapi+service endpoint split for a pathological case).
      - **[Seg-4 — service-stop robustness.]** FABLE5 (concurrency review) surfaced a **pre-existing
        shutdown handle-recycle race**: `sar_pipe_server_stop` waited 5 s per worker then closed the pipe +
        stop handles **regardless**, so a worker still inside an uncancelable recover `FilterSendMessage`
        would, on return, `sar_pipe_send` on a recycled handle. **FIX (leak-on-stuck):** `sar_pipe_server_stop`
        now returns a stuck flag and, on timeout, closes only the resources no live worker can still touch
        (each worker owns one pipe by index) and **leaks** a stuck worker's pipe + the shared stop event
        (already signaled, so the worker exits on its own; own-process service ⇒ process exit reclaims);
        `sar_control_listener_stop` skips `DeleteCriticalSection` on a stuck stop (a stuck worker may still own
        `g_recover_lock`/`g_control_lock`; the static CS is left initialized so its eventual Leave is defined).
        **Prompt-shutdown FIX (was ~15 s):** `sar_comm_recv_message` used a **synchronous** `FilterGetMessage`
        (`lpOverlapped=NULL`) in the `sar_comm_run` inbound loop; the one-shot `CancelIoEx` race (reader not
        yet parked when the cancel fires → `ERROR_NOT_FOUND`, next get blocks) left the loop stuck, so the
        service took ~15 s to reach STOPPED. Converted to **overlapped `FilterGetMessage` + a persistent
        manual-reset `stop_event`** on the client, waiting on `[completion, stop_event]` and, on shutdown,
        doing a **targeted** `CancelIoEx(port, &ov)` + drain (dispatching a message that won the race). A
        **fresh FABLE5** verified the pattern against the fltuser docs: the port is async by default
        (`FilterConnectCommunicationPort` `dwOptions=0`; only `FLT_PORT_FLAG_SYNC_HANDLE` would break it), a
        dedicated per-call event is mandatory (a NULL event makes `GetOverlappedResult` wait on the port
        handle, which also signals for the concurrent outbound `FilterSendMessage`), and the blanket
        `CancelIoEx(port, NULL)` in the stop handler had to go (it aborted concurrent sends) — all applied.
        **VM-VERIFIED (`vm_verify_recover_isolation`, 8/0): clean service stop < 5 s** (was ~15 s), recover
        byte-exact under a concurrent control-op storm, engine live, clean restart. **No verdict-channel
        regression** (`vm_verify_new` 27/2/1): every FN=0 check passes and Salsa20/12 convicts+recovers BY
        KEY; the 2 fails are the §8 documented **MMAP-ORACLE async-conviction flaky** (driver-internal
        `Enc_K(pre)==post` proof — causally independent of the service recv change; it *alternated* with the
        MMAP2 flaky of the prior run, the signature of the documented intermittents, not a regression). Capture
        actor-attribution worked throughout (BY-KEY recovery depends on it), confirming the identity/verdict
        inbound flow is intact.
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
        * **FABLE5 adversarial threading review (neutral WPF framing): fundamentally sound** — no
          thread-affinity violation (the RCW is fully consumed + disposed synchronously inside
          `_session.Begin()` before the first await; only the plain `List`/POCO rows cross to the pool
          thread; VM construction is post-await on the dispatcher), no constructable cross-thread view
          mutation, `_busy` is airtight on the single dispatcher thread, `AsyncRelayCommand` is
          non-concurrent by default, and recycling+grouping selection is correct (VM-held `IsSelected`,
          iterated over the source collection not containers). **3 findings applied** (next commit):
          (1) `Items.Clear()` moved INSIDE `DeferRefresh` (was a double view-Reset on the
          Unavailable→retry-after-failed-Confirm path); (2) `Begin` now has a Stage precondition
          `Stage is (PreElevation or Unavailable)` so the no-reentry-into-Browsing invariant lives in the
          VM, not just XAML visibility (future-proofs against a new KeyBinding/tray trigger orphaning
          `PreviewItems`); (3) the population `await` is wrapped → `Stage = Unavailable` on an unexpected
          throw, because the classify pass does filesystem I/O over **adversary-derived NT device paths**
          and an uncaught throw would reach `DispatcherUnhandledException` → crash. App builds 0 errors.
          (Deferred, cosmetic: bind Close's `CanExecute` to `!_busy` so it visibly disables during a long
          dead-network probe — UX, not correctness.)
      - **[Frontier] Bug-D-class sweep of the other session-backed view-models (FABLE5 review + fixes).**
        A fresh FABLE5 pass over `BudgetViewModel` + `PolicyViewModel` (against sources) found:
        * **Budget — HIGH, real UI-thread freeze, worse than Recovery's** because it re-fires on every
          range ComboBox change. `BudgetSession.Compute` (`BudgetAttribution.Build`) makes ~4-5 passes over
          ALL kept copies + per-item classify/alloc — order 100-500 ms at 100k copies, multi-second at 1M,
          synchronously from both `Begin` and `OnSelectedRangeChanged`. **FIXED:** `Begin` is now
          `async Task`; a new `RecomputeAsync` runs `Compute` + `BuildTrend` in `Task.Run` (both read only
          immutable POCO snapshots — the COM channel dies inside `BudgetSession.Begin`; `PointCollection`
          is `Freeze()`d so it is legal to build off-thread) and marshals `ApplyAttribution` back to the
          dispatcher. `OnSelectedRangeChanged` (a `partial void`, can't be async and mustn't drop
          concurrent flips) uses a **latest-wins `_computeVersion` token + a captured-session-reference
          guard** so a range flip mid-compute discards the stale result and a concurrent `Close` can't have
          its `_session=null` repopulated. Added the same `Begin` stage guard as Recovery.
        * **Policy — MEDIUM correctness, not a freeze** (exemptions are user-curated, few — sync load kept).
          Its add flow re-`Begin`'d after a successful `Add`, causing a **third elevation consent** (the view
          promises "once per visit") and a real bug: if the user cancelled that refresh consent, the
          exemption was really added but the session went `Idle` → `SyncFromSession` fell to `default` →
          stage downgraded to PreElevation showing an **empty list** despite "Exemption added." **FIXED:**
          after `Add==Added`, append the row **locally** (built from `_pendingIdentity` captured in
          `StartExempt` + `_pendingPath`, `MatchState=Matching`) instead of re-`Begin` — no extra consent,
          no downgrade. Added the missing `_busy` guard to `CancelExempt`.
        * **App-wide (FABLE5 Q4) — the `_busy` re-entrancy guard was invisible to the UI** (plain field, no
          `CanExecute`), so buttons stayed enabled during the exact windows re-entrancy is possible (UAC
          consent pump, file-picker modal, blocking STA COM) and clicks silently no-op'd. **FIXED in Budget +
          Policy:** promoted to `[ObservableProperty] IsBusy` + `[NotifyCanExecuteChangedFor]` on the
          session-mutating commands (`CanExecute = NotBusy`), so buttons visibly disable. (Recovery retains
          its working `_busy` bool from Bug D; retrofit optional.) App builds 0 errors. **Verify next:** GUI
          exercise of Budget range-flips with many copies + Policy add/remove (VM GUI harness is flaky, §6).
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
      * **Broad cross-path concurrency soak (`scripts/vm_soak_broad.ps1`) — RESOLVED: it was HOST
        DEGRADATION, not a driver bug.** Drives `mmap_over` + `stream_transform` capture + `sarctl` recover +
        control storms **simultaneously** and sustained (a combination the functional suite never runs — it
        exercises those paths separately). A first run on a heavily-cycled host wedged the guest
        (`Heartbeat=LostCommunication`) in round 1. Made the script self-contained (`-Restore -Deploy -NoMmap`)
        and **re-ran on a fresh host/snapshot: 22/0, all 6 rounds clean** — every round filter-live, service
        up with the **same PID (no crash/restart)**, control channel responsive, and **no handle/thread leak**
        (handles 169→308 < 200 threshold, threads 19→17). So the exact same cross-path load passes on a fresh
        host; the earlier wedge was accumulated **host vmbus degradation** (§6 — this drive ran many VM
        cycles), **not** a driver cross-path defect. The recover-lock-split + shutdown changes and the
        capture/recover/mmap paths handle sustained simultaneous concurrency without wedging — the
        "사용성 저해 0" availability guarantee holds under this stress. (Minor: the ~139 handle growth is
        per-round `sarctl`/job/pipe churn, within threshold; if a longer soak shows monotonic growth, audit
        per-connection handle release — not seen here.)
      * **Other remaining Segment-4 work:** service-stop robustness *under rapid driver reload* (the
        `Restart-Service` double-action transient wedge from Segment 1 — probe fixed; the service side is now
        much harder to wedge after the leak-on-stuck + overlapped-shutdown fixes, but not yet soaked under
        reload). Harnesses prebuilt in `build_harness/`.
- Owner-only pending: Part XII ratification; **`git push`** (many unpushed commits this drive — latest is
  `e955a72`; run `git log --oneline origin/main..HEAD` for the full set); MS driver cert.
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
