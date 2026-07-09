# semantics-ar — FRONTEND HANDOFF

> **This handoff is optimized for the frontend workstream, which is the active work.**
> The backend (driver + service, units T1–T3) is **complete, VM-verified, and sealed** — do
> not modify it; treat `docs/CONSTITUTION.md` as fixed. This document is a rewrite: the prior
> whole-project handoff remains in git history if you need it.
>
> **Governing documents, in precedence order — obey them, amend them there first, never
> silently substitute an easier design:**
> 1. `docs/CONSTITUTION.md` — what the system proves and does (authoritative, RATIFIED).
> 2. `docs/EXPERIENCE_CHARTER.md` — how it is presented to a human (RATIFIED, subordinate).
> 3. `docs/FRONTEND_DESIGN.md` — the frontend spine, v1.1 RATIFIED (subordinate to both). Its
>    **Appendix A** is the Slice-1 build & VM-verification record.
>
> The visual target is the owner-approved contract-aware mock:
> `frontend/mocks/recovery-and-budget.v1.html` (open it; read its `README.md`).

---

## 0. BINDING RULES (non-negotiable)

1. **Code has NO comments.** Write code that reads like the surrounding code.
2. **No dead / compatibility / fallback code.** The project has never shipped — no migration
   code, no schema bumps, no compat shims. Every line is used.
3. **VM-verify every nontrivial change** against the running backend in `SarTarget`. Review does
   not substitute — every real defect this session (event-class gap, toast spam, the phantom
   deploy interaction) was caught only by *running*, not by reading.
4. **Honesty guardrails** (Constitution / Charter / FRONTEND_DESIGN): never surface key material,
   IVs, verification tags, or preserved plaintext (I.12); never disclose a phantom/decoy identity
   (I.11); expose **no detection knob** — the only user-tunable value is the budget
   (retention + capacity); calm by default, no scareware; absolute words ("verified",
   "byte-for-byte") are scoped to the per-file mechanism, never the product.
5. **The two-plane split is enforced by TYPE.** Posture-plane DTOs carry no path and no free text
   (enum/numeric only); anything itemized or consequential goes over the elevated pipe. This is a
   proof obligation (FRONTEND_DESIGN XIII.4), not a convention.
6. **Reuse what genuinely fits the ratified design; discard what does not.** Do not preserve
   structure, naming, or flow by inertia. The existing `frontend/` largely fits — verify against
   the current wire (`frontend/sarapi/include/sarapi.h`) before trusting any file.
7. **The exemption anchor stays content-hash ∧ certificate-subject** (FRONTEND_DESIGN X.3). Do
   NOT relax it to signer-only / min-version, and do not silently auto-heal an exemption on app
   update — that was reviewed and rejected as unconstitutional. Re-affirm is calm and manual.

---

## 1. WHERE THINGS STAND (2026-07-10)

- **Ratified specs:** `CONSTITUTION.md`, `EXPERIENCE_CHARTER.md`, `FRONTEND_DESIGN.md` (v1.1).
- **Mock:** `frontend/mocks/recovery-and-budget.v1.html` — Recovery + Budget/Exemption hero
  flows, contract-aware (a "Show data provenance" toggle maps every value to a wire field or a
  named precondition). Note: the mock says "PROTECT mode" as a neutral placeholder; the real
  modes are **AUDIT / ENFORCE**.
- **Slice 1 (posture plane): DONE and VM-verified 9/0.** See §3.
- **Everything else: not started.** The itemized/elevated hero flows, the backend projection
  amendments, packaging/signing, onboarding, and the operational tier are the bulk of the
  remaining work. See §4.

---

## 2. ARCHITECTURE & STACK (as ratified, as built)

- **Topology (FRONTEND_DESIGN II.3):**
  - `frontend/sarapi/` — native C, owns the binary wire for **both** planes; already implements
    the posture read, the events stream, the elevated control path, and the III.7 client-side
    server-is-SYSTEM check (`server_identity.c`). Built as `sarapi.dll`.
  - `frontend/SemanticsAr.Core/` — .NET library: `NativePostureReader`, `PostureService`
    (polling + stale tolerance + `AuditAcknowledged`), `PostureEvaluator` (verdict per V.4),
    `JournalService` (events: background reconnect, (gen,seq) dedup, gap detection), redaction
    DTOs (`SarApiPosture` 40B, `SarApiEvent`, both no-path), and the elevated-channel scaffolding
    (`ElevatedControlChannel`, `NativeMethods.Com`) for the itemized plane.
  - `frontend/SemanticsAr.App/` — WPF, **standard-user**: composition in `App.xaml.cs`
    (posture → Home + tray; events → `ToastNotifier`), `TrayIconController`, `ThemePalette`
    (WPF-UI), `ProcessHardening`, the surfaces/viewmodels.
  - `frontend/elevation-host/` — COM local server (elevated) for the itemized plane; not yet
    wired/verified.
  - `frontend/SemanticsAr.Verify/` — self-contained headless harness that drives Core against
    the live backend (the Slice-1 VM-verification vehicle).
- **Stack (ratified, present in `SemanticsAr.App.csproj`):** .NET 10, WPF, **WPF-UI 4.3.0**,
  CommunityToolkit.Mvvm 8.4.0, **H.NotifyIcon.Wpf 2.4.1**, Microsoft.WindowsAppSDK.
  Framework-dependent deployment.
- **Wire (three pipes):** posture (`\\.\pipe\SemanticsAr.Posture`, session-readable, no path),
  events (`\\.\pipe\SemanticsAr.Events`, redacted stream), control (elevated, SYSTEM+Admins,
  ECDSA handshake). Headers: `common/include/semantics_ar/{protocol.h,posture.h}` and
  `frontend/sarapi/include/sarapi.h`. Protocol version = 1.
  - The service's pipe DACLs (authoritative, `service/control.c`): control =
    `D:P(A;;GA;;;SY)(A;;GA;;;BA)` (elevated only); posture/events =
    `D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;0x120189;;;IU)` (read-only to Interactive Users, no
    `FILE_CREATE_PIPE_INSTANCE`, `FILE_FLAG_FIRST_PIPE_INSTANCE` anti-squat).

---

## 3. SLICE 1 — DONE (posture plane)

**Built:** `sarapi.dll`; Core aligned to §V.4 (acknowledged AUDIT collapses to green/neutral;
`PostureService.AuditAcknowledged` added); `SemanticsAr.Verify` harness. Full frontend builds
0 errors; Core unit tests 69/69.

**VM-verified 9/9 PASS against `SarTarget`:** sarapi ABI compatible; posture DTO is a fixed
40-byte frame with **no path-capable field** (II.2, proven by reflection + size); event DTO
no-path; posture read **OK** (session-readable; III.7 server-is-SYSTEM passed); verdict =
**Amber/AuditMode** (correct unacknowledged-AUDIT, V.4); protocol version matches (no skew);
service running; driver connected; **events pipe connected + 238 events flowed** (incl.
`ModeChanged`). Closes the core of XIII.4(a)/(b) at the Core layer against the real backend.

**Fixed this session:** `JournalEventClass` + `sarapi.h` extended with `BlockInjection=8`,
`ExemptRevoked=10` (driver emits these; enum stopped at 7). `ToastNotifier` now coalesces
detection events in a 3s window into one count-aware toast per class (anti-habituation, V.1);
`BLOCK_INJECTION` and housekeeping classes stay silent by design (verified against the 238-event
flood → 0 toasts).

**Remaining Slice-1 increments (App presentation, over the VM-verified Core):**
- Wire the §V.4 **acknowledge-AUDIT** action to `PostureService.AuditAcknowledged` in the Home
  surface.
- Aesthetic polish of the Home hero + tray glyph to the mock's certainty-ladder language.
- Run the **actual WPF GUI** in the VM (or an interactive session) for the owner's visual pass —
  only the Core seam has been run against the real backend so far.
- The medium-IL **interactive** IU-DACL runtime proof (FRONTEND_DESIGN XV.3 residual — currently
  source-verified; Run-1 passed via the admin/BA grant, not the IU grant).

---

## 4. REMAINING WORK, PRIORITIZED

**A. Finish Slice-1 App increments** (§3, small).

**B. Backend Constitution-projection amendments** — these gate the itemized surfaces; each is a
governed amendment (Const. III.1.3 discipline) + VM-verify, never invented in the UI:
- **XII.2 per-copy app attribution — HIGHEST** (forward-accruing; every week of delay = a week of
  "unattributed" data at launch). Needed for the Budget-attribution flow.
- **XII.1 per-item pool status** (BOUNDED honest-by-pool). Until it lands, BOUNDED shows the
  conservative probation-honest wording.
- **XII.4 preserve actor key** (BOUNDED incident grouping; catalog already has it, preserve does
  not).
- **XII.3 integrity-halt posture signal** (drives the red + foreground-window tier).

**C. Slice 2+ — itemized / elevated plane (the mock's hero flows):**
- **Recovery** (FRONTEND_DESIGN VIII): catalog + preserve queries over the elevated pipe via the
  `elevation-host` COM moniker; incident grouping (DEFINITIVE by actor_start_key + capture_time,
  already projected); preview → verify → loud report; **"modified since T" split, unchecked by
  default** (the flagship safety moment).
- **Budget attribution + exemption discovery** (IX/X): time-denominated header; sorted ranked
  app list with bars + sparkline; exempt confirm (identity inline, ladder-terms consequence,
  quantified cost); calm staleness re-affirm; interpreter hard-stop; "unattributed" bucket first.
- **Elevation model** (III): one elevation fetches an itemized snapshot the standard-user app
  browses without re-elevation; each mutating action = its own fresh consent; no resident
  elevated broker; client verifies server=SYSTEM.

**D. Onboarding hero flow** (VIII.5): pre-empt SmartScreen/driver-trust prompts; explain the two
assets + coverage line; default AUDIT; pin the tray.

**E. XV governance / execution gates:** legal claim clearance (X.1); posture DACL Logon-SID vs IU
decision (XV.3); Administrator Protection re-verification incl. `TokenElevationTypeFull`
semantics (XV.4); offboarding disposition (XV.6); multi-user ownership (XV.7); clock integrity
(XV.8).

**F. Packaging / signing & installer:** Authenticode-sign the app; installer detects + installs
the .NET 10 Desktop Runtime as a prerequisite; the installer **writes new files, never
bulk-deletes/overwrites a monitored directory** as a non-exempt process (see §6 phantom finding).

**G. Operational DoD tier** (XIV): Windows-10 QA, High Contrast, pseudo-localization, performance
budgets (idle memory, large-list virtualization with UIA active), crash dumps treated as
sensitive (XI.2 — a dump can contain an itemized snapshot).

---

## 5. HOW TO BUILD & VM-VERIFY (pick up cold)

This machine has .NET 10 SDK, CMake, Visual Studio 2022, and Hyper-V. VM **`SarTarget`** runs the
sealed backend.

- **Build `sarapi.dll`:** `cmake --build build_fe --target sarapi --config Release` (uses the
  repo-root `CMakeLists.txt` tree; driver/ELAM skip cleanly without WDK). Output:
  `build_fe/frontend/sarapi/Release/sarapi.dll` — the App/Verify csproj link this. Do NOT
  configure `sarapi/` standalone (its `CMakeLists.txt` has no `project()`).
- **Build the frontend:** `dotnet build frontend/SemanticsAr.App/SemanticsAr.App.csproj -c
  Release`. Tests: `dotnet test frontend/SemanticsAr.Core.Tests/SemanticsAr.Core.Tests.csproj`.
- **Verify harness:** `dotnet publish frontend/SemanticsAr.Verify/SemanticsAr.Verify.csproj -c
  Release -r win-x64 --self-contained true -p:PublishSingleFile=true` → `sarverify.exe` +
  `sarapi.dll`.
- **VM access:** PowerShell Direct —
  `New-PSSession -VMName SarTarget -Credential (admin/admin)`. After a fresh boot the driver is
  loaded but the **service is stopped**: `Start-Service semantics_ar_service`, then
  `C:\sar\sarctl.exe mode audit` and `... budget <retention_100ns> <capacity_MB>` to populate
  posture. A full backend (re)deploy uses the ceremony in `scripts/vm_verify_new.ps1` (restore
  snapshot `clean-baseline-20260704`, deploy signed driver+service+sarctl+harnesses, load the
  minifilter, start the service).
- **Deploy the frontend into the VM as FEW new files** (the single-file `sarverify.exe` +
  `sarapi.dll`) with `Copy-Item -ToSession`. Do NOT `Expand-Archive`/bulk-write into a monitored
  directory (see §6). The clean VM has no .NET runtime → publish self-contained.
- **`sarctl.exe`** (`C:\sar` in the VM) is the diagnostic CLI: `mode`, `budget`, `list`,
  `preserve-list`, `events`, `recover`, `preserve-recover`, `whitelist-add/remove`, `resolve`,
  `verdict`, `procquery`, `inflight`.

---

## 6. GOTCHAS / FINDINGS (do not relearn the hard way)

- **Phantom-witness directory seeding.** Creating a directory under a monitored volume gets
  **protected decoy files** planted (Const. VIII); a non-exempt process's bulk delete/overwrite
  there is **denied** (this breaks `Expand-Archive -Force`). New-file writes with distinct names
  succeed — deploy two files individually. The app's normal new-file writes to its own storage
  are fine; the **installer** must not bulk-delete a monitored dir.
- **Event volume.** `BLOCK_INJECTION` (class 8) can flood (238 in ~7 s). It is silent by design
  (no toast string) — keep high-volume classes silent/aggregated; never per-event toast (V.1).
- **`runas /trustlevel` is not a clean medium-IL test.** It yields a SAFER-restricted token that
  still reports HIGH integrity and cannot `OpenProcess` the SYSTEM service, so the III.7 check
  returns `SERVER_UNTRUSTED` (a false negative). Use a real standard user / interactive logon /
  linked token for the medium-IL IU-DACL proof.
- **`_Static_assert`** in the C clients needs `/std:c11` if you compile them directly (the
  CMake target already sets C11).
- **Warnings** `MVVMTK0045` (WinRT-AOT advisory on `[ObservableProperty]` fields) are harmless
  for WPF; not an error.

---

## 7. DEFINITION OF DONE (per slice)

A slice is done only when it satisfies the Charter Part XI and Constitution Part XI conformance
checklists **and is VM-verified against the running backend**, including the operational tier
(FRONTEND_DESIGN XIV). Prove, don't assert (XIII.4): the no-path and no-prompt guarantees are
test artifacts, not review claims.
