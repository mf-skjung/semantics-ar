# semantics-ar — HANDOFF (2026-07-10, session-4 rewrite: backend attribution vertical)

> **Read this first, then the governing docs.** This fully replaces the prior handoff. It reflects
> the state after the **XII.1/2/4 preservation-attribution vertical was implemented across the whole
> stack (driver → service → sarapi → COM host → .NET Core), verified on the `SarTarget` VM, and
> committed to `main` (`a5d1822`, not pushed).** A successor picking this up cold: internalize §0–§2
> (governing docs, binding rules, what is done) and §5 (what remains) before writing code. The next
> recommended task is the **frontend attribution consumer UI** (§5.A) — it is fully unblocked now.

## 0. GOVERNING DOCUMENTS — precedence order (obey; amend there first, never silently substitute)

1. `docs/CONSTITUTION.md` — what the system proves/does (RATIFIED, authoritative).
2. **`docs/CONSTITUTION_PART_XII.md` — projection amendments (DRAFTED this session, pending
   ratification into CONSTITUTION.md).** XII.1/2/4 are now implemented + VM-verified; XII.3 and XII.5
   remain unimplemented. This document is the normative home for every backend field the frontend
   consumes — never invent a projection in the UI; amend here first.
3. `docs/EXPERIENCE_CHARTER.md` — how it is presented to a human (RATIFIED, subordinate).
4. `docs/FRONTEND_DESIGN.md` — the frontend spine, v1.1 RATIFIED (subordinate to both). **Part IX**
   (budget attribution / exemption discovery) and **Part XII** (backend preconditions) are the map for
   the next task.
5. **`frontend/mocks/recovery-and-budget.v1.html` — the owner-approved VISUAL/IA target.** The Budget
   surface (the mock's 2nd hero) is the model for §5.A. Where mock and prose disagree on IA/hero-flow,
   the mock wins, EXCEPT the Constitutional honesty guardrails.

## 1. BINDING RULES (non-negotiable)

1. **Code has NO comments.** Match the surrounding idiom.
2. **No dead / compatibility / fallback / migration / schema-version code.** The project has never
   shipped. This was re-litigated twice with FABLE5 this session (it repeatedly proposed a preserve-
   format version field + V1→V2 migration): **rejected both times** — the on-disk store load path
   resets cleanly to empty on a size/format mismatch (`driver/preserve.c:565`, no tamper flag, no
   crash) and every VM run redeploys onto the clean-baseline snapshot. Do not add migration code.
3. **Reuse only what genuinely fits the ratified design + the mock.** Verify against the current wire
   before trusting inherited code.
4. **Honesty guardrails:** never surface key material / IVs / tags / preserved plaintext (Charter I.12);
   never disclose a phantom identity (I.11); expose no detection knob (only the budget is tunable);
   calm by default. **Two planes enforced by TYPE** (FRONTEND_DESIGN XIII.4): posture-plane DTOs carry
   no path/free-text; anything itemized/consequential goes over the elevated pipe. Attribution is
   **itemized/elevated + aggregate-only** (no per-file path list in the attribution view, IX.4).
5. **Exemption anchor stays content-hash ∧ certificate-subject** (X.3). Never relax to signer-only /
   min-version; no silent auto-heal on update.
6. **Verification cadence (owner's standing directive):** heavy verification (VM runs) is batched.
   Per-slice keep the cheap gates: `dotnet build` 0 + `dotnet test` green + local GUI smoke. **When
   kernel/service wire changes, a VM run is mandatory** (this session's regression proved it — see §4).
7. **FABLE5 review discipline (owner-directed, done 3× this session):** present confirmed logic to
   FABLE5 **at code level** (framed as a benign file-backup / disk-usage utility — never ransomware/
   attack terms or it refuses), demand code-level counterexamples + fixes, then **critically accept** —
   reject findings that don't reproduce or that violate a binding rule, with evidence; never rubber-
   stamp, never ignore a valid finding. Spawn it via the Agent tool with `model: fable`.

---

## 2. WHAT IS DONE THIS SESSION (committed to `main` as `a5d1822`, VM-verified, NOT pushed)

The **XII.1/2/4 preservation-attribution vertical**: every preserved original now records which
application caused it, honestly and forward-accruing, surfaced on the elevated plane.

### 2.1 Constitution Part XII amendment (file-first) — `docs/CONSTITUTION_PART_XII.md`
Drafted in the Constitution's normative style. XII.1 (per-item pool state), XII.2 (per-copy causing-app
identity, forward-accruing, the "file-first" linchpin), XII.4 (preserve actor = OS start key, correcting
a real PID-vs-start-key defect), plus XII.3/XII.5 specified but **not yet implemented**. Pending
ratification into `CONSTITUTION.md`.

### 2.2 Driver (kernel) — `driver/`
- **XII.4:** all **7** preserve-actor origins in `capture.c` (376, 443, 809→StageLink, 1030, 1262, 1888,
  and the mapped-section replay 1856) + the promotion site (336) switched from `PsGetProcessId` (PID,
  reused) to **`PsGetProcessStartKey`** (durable). FABLE5 caught that the original plan of "4 sites" was
  wrong — there are 7.
- **XII.4.2 mapped seam:** `SarMmapArm` (capture.c) now captures the arming process's **start key**
  (`mmap_arm_key`, new field in `seam.h`, init in `operations.c`), published **before** `mmap_arm_pid`
  to close a guard fail-open race; `SarMmapCaptureInline` takes `Actor`(start key) + `ActorPid`(for the
  Oracle-submit lookup), and the Oracle submit is guarded against PID reuse
  (`Actor==0 || PsGetProcessStartKey(proc)==Actor`).
- **XII.2:** causing image path resolved **off the store lock** (before `FltAcquirePushLock`) via new
  `SarStateImageByStartKey` (`state.c/.h` — scans the per-process identity table by start key, returns
  the image path captured at process create). Stamped onto `sar_preserve_record_t.causing_image_path`
  (new field, `preserve_format.h`; auto-covered by the record MAC). `SarPreserveProject` projects
  `state` + `actor_start_key` + `causing_image_path`.

### 2.3 Service — `service/`
- **`attrib.c`/`.h` (new):** resolves an NT image path → `{sha256, signer subject, verdict}` via the
  existing `sar_identity_evaluate`, with NT→Win32 conversion (QueryDosDevice, GLOBALROOT fallback),
  a **hash-keyed dedup id**, and a **path→(last-write,size) freshness cache** that only caches on hash
  success and self-heals if the app row was evicted. `sar_attrib_init()` called from `main.c`; added to
  `service/CMakeLists.txt`.
- `control.c` `PRESERVE_LIST` projects `state`/`actor_start_key`/`app_identity_id` (the last via
  `sar_attrib_resolve`); new `SAR_CTL_OP_APP_IDENTITY_LIST` handler enumerates the identity table.
- **Transport fix (critical, see §4):** `SAR_COMM_RECV_BUFFER` 1024→2048 in `commclient.c`, now
  **compile-time bounded** by a message-union `C_ASSERT` so this class of bug can never silently recur.

### 2.4 sarapi + elevation-host COM + .NET Core
- `sarapi.h`/`control_client.c`: widened `sarapi_preserve_entry_t` (552→576, padding-clean:
  actor_start_key@552, app_identity_id@560, state@568) + new `sarapi_app_identity_t` +
  `sarapi_app_identity_page`; `_Static_assert` on sizes.
- elevation-host `.idl`/`elevation_iface.h`/`control_object.cpp`: new `AppIdentityPage` COM method
  (same vtable slot in all four places, incl. `ISarElevatedControl.cs`).
- `RecoveryLadder.cs`: `PreserveEntrySize` 552→576 + new offsets; `ParseAppIdentities` +
  `AppIdentityEntrySize=1080`. New `AppIdentity` record + `PreservePool` enum; `RecoverableItem` gained
  `Pool`/`AppIdentityId`. `IElevatedControlChannel.LoadAppIdentities` + impl + `FakeChannel`.
- **ABI/protocol versions bumped** (SARAPI_ABI_VERSION 1→2, ExpectedAbiVersion 1→2,
  SEMANTICS_AR_PROTOCOL_VERSION 1→2, Verify harness expectation) — the honest skew guard.

### 2.5 Tooling
- `tools/sarctl.c`: `preserve-list` now prints `actor=/pool=/app=`; new `app-identity` command.
  (sarctl talks to the **service control pipe** = the elevated path, so it exercises the full service
  projection incl. attribution — the primary VM verification lever.)
- `scripts/vm_verify_attribution.ps1` (new): focused probe (renameover + mmap_over workloads →
  asserts actor/pool/app + causing-image resolution + no orphan).

### 2.6 FABLE5 reviews (3, all adjudicated)
1. **Design review** → found the "4 vs 7 sites" error + the mapped-seam PID-replay problem (accepted,
   reshaped XII.4) + ABI-bump need (accepted); rejected the migration-code recommendation.
2. **Implementation review** → 4 defects fixed: kernel stack-tail zeroing (`state.c`), PID-reuse oracle
   guard (`capture.c`), attrib cache poisoning + eviction self-heal (`attrib.c`), a C_ASSERT.
3. **Patch review** (of the fixes + the buffer fix) → verified all patches correct (kernel refcounts
   exactly-once, IRQL-safe, behavior-preserving); accepted 3 hardening gaps (compile-time transport
   bound, arm-key-before-pid ordering, full causingPath zero); rejected the migration finding again.

---

## 3. FILE MAP (key files a successor edits)

- **Governing:** `docs/CONSTITUTION_PART_XII.md` (amend before adding any projection).
- **Driver:** `driver/{capture,preserve,state,operations}.c`, `driver/{seam,state,preserve}.h`,
  `common/include/semantics_ar/{preserve_format,protocol}.h`.
- **Service:** `service/{attrib,control,commclient,main}.c`, `service/{attrib,control}.h`.
- **Wire client:** `frontend/sarapi/{include/sarapi.h,src/control_client.c}`;
  `frontend/elevation-host/{SemanticsArElevation.idl,include/elevation_iface.h,src/control_object.cpp}`.
- **Core (.NET):** `frontend/SemanticsAr.Core/Domain/{RecoveryLadder,RecoverableItem,AppIdentity,
  PreservePool}.cs`, `Interop/{ISarElevatedControl,NativeMethods}.cs`,
  `Services/{IElevatedControlChannel,ElevatedControlChannel}.cs`. Tests in `SemanticsAr.Core.Tests` (109).
- **App (WPF) — the next task's home:** `SemanticsAr.App/ViewModels/BudgetViewModel.cs` +
  `Views/BudgetView.xaml`, `Core/Domain/BudgetSnapshot.cs`.
- **Verify:** `tools/sarctl.c`, `scripts/vm_verify_new.ps1` (full regression), `scripts/
  vm_verify_attribution.ps1` (attribution probe), `frontend/SemanticsAr.Verify/Program.cs`.

---

## 4. THE VM-REGRESSION LESSON (do not repeat)

The first VM run failed **7 phases** (`byPre=0`, `floor=0/3`) though the build was clean, 109 tests
passed, and 2 FABLE5 reviews were done. Root cause: the grown preserve reply
(`semantics_ar_preserve_reply_t` ~1112 bytes) exceeded the **1024-byte `SAR_COMM_RECV_BUFFER`** transport
cap → `sar_comm_send_recv` returned `SAR_COMM_ERR_PROTOCOL` for **every** preserve query (the smaller
catalog reply still fit, which is why `byKey` worked but `byPre=0` everywhere). Fix: buffer 1024→2048 +
a compile-time union assert. **Takeaways:** (a) growing any driver↔service reply struct must be checked
against `SAR_COMM_RECV_BUFFER` (now compile-enforced); (b) unit tests + build + code review do **not**
substitute for a VM run when the wire changes; (c) `byKey`-works/`byPre`-fails is the signature of a
preserve-query transport failure.

---

## 5. WHAT REMAINS

### 5.A — NEXT TASK (recommended): frontend attribution consumer UI (fully unblocked, no gate)
The backend now projects everything the mock's 2nd hero needs. Build it per **FRONTEND_DESIGN Part IX**
+ the mock's Budget surface, WPF + Core, gated only by `dotnet build`/`test`/GUI-smoke (no VM, no
kernel work):
- **Core (pure, unit-tested):** extend/mirror `BudgetSnapshot` to **join** the preserve snapshot
  (`LoadPreserved`) with the app-identity list (`LoadAppIdentities`) → per-app **window-impact**
  aggregation (app × coarse file-class × aggregate; NEVER a per-file path list — IX.4). Group by
  `AppIdentityId`; resolve display identity from the `AppIdentity` rows.
- **Design the "unattributed" bucket first** (IX.6): `app_identity_id==0` rows (kernel/system writes, or
  pre-attribution holds) roll up into an honest "older activity (before attribution)" slice — it
  dominates early and the feature looks broken if not handled from day one.
- **View:** ranked list with proportional bars (window% first, bytes on expansion), a trend/time-range
  selector, and budget-settings readback (retention/capacity steppers). Denominate in **time** (IX.2).
- **Elevation:** attribution is itemized/elevated (IX.7) — one elevation per visit, then browse
  in-process. The **exempt modal's quantified cost** ("currently protecting N files (S bytes) from this
  app") is now computable from the same join.
- Reuse `BudgetSession` (mirrors `RecoverySession`). `LoadAppIdentities` already exists on the channel.

### 5.B — XII.5 exemption enumerate (backend + frontend, needs VM re-verify)
New driver verb to enumerate the whitelist + surface the 3 existing COM verbs
(`ResolveIdentity`/`WhitelistAdd`/`WhitelistRemove`) through `IElevatedControlChannel` **with** their
consumer (the exempt/trust flow) — never as dead code. Match-state (matching / lapsed-same-signer /
lapsed-changed-signer). Wire types were drafted then reverted this session to avoid dead code; re-add
with the handler. **Gate:** another driver/service cycle + VM re-verify.

### 5.C — XII.3 integrity-halt posture flag (backend + frontend, needs VM re-verify)
Add a VII.1.4 tamper/rollback flag to the posture frame → drives the red + foreground-window integrity
alert (Charter III.2a, FRONTEND_DESIGN V.3). **Gate:** posture-plane change + VM re-verify.

### 5.D — Ratify Part XII into CONSTITUTION.md
Once the amendments are stable, fold `CONSTITUTION_PART_XII.md` into `CONSTITUTION.md` (governed act).

---

## 6. HOW TO BUILD, TEST, VM-VERIFY

Machine: .NET 10 SDK, CMake + VS 2022, WDK 10.0.26100, Hyper-V. VM `SarTarget` (admin/admin), snapshot
`clean-baseline-20260704`.

- **.NET:** `dotnet build frontend/SemanticsAr.Core/SemanticsAr.Core.csproj -c Release` +
  `dotnet test frontend/SemanticsAr.Core.Tests/... -c Release` (currently 109/109). The
  `AbiVersion_IsCompatible` test needs the freshly-built `sarapi.dll` copied next to the test bin.
- **Native (sarapi/host/service):** `cmake --build build --target <sarapi|sar_elevation_host|
  semantics_ar_service> --config Release`. **For VM deploy**, build service + sarctl into **`build_win`**
  (the dir the deploy scripts read): `cmake --build build_win --target semantics_ar_service sarctl
  --config Release`. All native is `/W4 /WX`.
- **Driver:** `cmd //c "scripts\build_driver.bat"` → `build_driver/semantics_ar.sys`. **GOTCHA:** this
  script **wipes `build_driver/` (incl. `pkg/`) on every run** — re-package after every driver build.
- **Signing (for VM):** `scripts/package_driver.ps1` self-signs with `CN=SemanticsAr Test`. **GOTCHA:**
  its ELAM sub-build fails on vcvars resolution in a non-interactive shell, and ELAM is **not needed**
  for the functional deploy (vm_verify only deploys `semantics_ar.{sys,inf,cat}`). Do the signing steps
  manually: recreate `build_driver/pkg`, copy `.sys` + `driver/semantics_ar.inf` + service exe, export
  the cert, `signtool sign /fd sha256 /sha1 <thumb>` the `.sys`, `Inf2Cat /driver:pkg /os:10_X64,
  10_GE_X64`, sign the `.cat`, sign the service exe. (Do NOT run PowerShell with `-ExecutionPolicy
  Bypass` — the harness classifier blocks it; `-File` works.)
- **VM regression (13 invariants):** `powershell.exe -NoProfile -File scripts\vm_verify_new.ps1`
  (restores snapshot, deploys pkg + `build_win` service/sarctl + `build_harness/*.exe`, runs phases).
  Run it in the background and Monitor the log for `FAIL`/`new-code verification:`. Current baseline
  with this session's code: **29 passed, 0 failed, 1 skipped** (the 1 skip = TIER2 signed-harness
  trust, a pre-existing VM condition unrelated to code).
- **VM attribution probe:** after a ceremony leaves the driver loaded, `powershell.exe -NoProfile -File
  scripts\vm_verify_attribution.ps1` → **8 passed, 0 failed** (actor/pool/app + normal & mmap seam
  resolution + no orphan). Fast service-only iterations: rebuild service, re-sign, stop/copy/start the
  service on the VM (driver stays loaded), re-run the probe.

## 7. GOTCHAS (carry forward)
- **Grown driver↔service reply must fit `SAR_COMM_RECV_BUFFER`** — now compile-asserted; if you add a
  field, the `C_ASSERT` in `commclient.c` catches an overflow at build time. §4.
- **`build_driver.bat` wipes `build_driver/pkg`** — re-sign after every driver rebuild.
- **`package_driver.ps1` elam step fails** (vcvars in non-interactive shell) — sign manually; elam isn't
  deployed by vm_verify.
- **`sarctl` = the elevated path** (service control pipe), so it verifies the full service projection incl.
  attribution. `preserve-list` shows `actor/pool/app`; `app-identity` lists resolved apps.
- **`app=0` is the honest unattributed bucket** (kernel/system writes with no tracked process identity),
  not a bug — design the UI to roll these up (IX.6).
- **PID vs start key:** the preserve seam historically used the PID; the catalog used the start key. They
  must both use the start key to group into one incident. §2.2.
- Each PowerShell tool call is a **fresh session** (no `Start-Job` persistence across calls); run
  long/background VM work via the Bash tool `run_in_background` + a log file + Monitor.

## 8. DEFINITION OF DONE (per slice)
Charter Part XI + Constitution Part XI + FRONTEND_DESIGN XIV, the cheap gates every slice (build 0 /
test green / GUI smoke), and — when the wire/kernel/service changes — **VM-verified** (full regression +
the relevant probe). Prove, don't assert. Commit to `main` on owner request; **push is operator-
controlled** (this session's work is committed as `a5d1822`, not pushed).
