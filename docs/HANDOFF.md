# semantics-ar — FRONTEND HANDOFF (2026-07-10, session-3 rewrite)

> **Read this first, then the governing docs.** This fully replaces the prior handoff. It reflects
> the state after the **3-surface IA was landed, the WPF GUI was run for the first time, the
> WinAppSDK deployment model was fixed, the mode-switch and onboarding flows were built, and two
> FABLE5 code reviews were adjudicated.** A successor picking this up cold: internalize §2 (what is
> done) and §5 (what remains + its gates) before writing code. The GUI now actually launches — use
> the smoke technique in §6 on every change.

## 0. GOVERNING DOCUMENTS — precedence order (obey; amend there first, never silently substitute)

1. `docs/CONSTITUTION.md` — what the system proves/does (RATIFIED, authoritative).
2. `docs/EXPERIENCE_CHARTER.md` — how it is presented to a human (RATIFIED, subordinate).
3. `docs/FRONTEND_DESIGN.md` — the frontend spine, v1.1 RATIFIED (subordinate to both).
4. **`frontend/mocks/recovery-and-budget.v1.html` — the owner-approved VISUAL/IA target.** Where the
   mock and FRONTEND_DESIGN's prose disagree on information architecture or a hero-flow interaction,
   **the mock wins** — EXCEPT the Constitutional honesty guardrails (prompt-free Home, no path leak,
   no detection knob) which win over the mock. Read `frontend/mocks/README.md`; the mock's IA is the
   3 surfaces now implemented. **Mode-switch and Onboarding are OUT of the mock's scope** (HANDOFF
   binding: "design them from the docs when reached, consistent with mock language") — they are now
   built from Charter VI.2.1 / VI.4.1 and get an owner feel-pass at the batched end (XIII.6).

## 1. BINDING RULES (non-negotiable)

1. **Code has NO comments.** Match the surrounding idiom.
2. **No dead / compatibility / fallback / migration / schema-version code.** The project has never
   shipped. Remove such traces if found. Do NOT add guards for inputs that cannot occur.
3. **Reuse only what genuinely fits the ratified design + the mock.** Verify against the current wire
   (`frontend/sarapi/include/sarapi.h`, `common/include/semantics_ar/*.h`) and the mock before
   trusting inherited code.
4. **Honesty guardrails:** never surface key material, IVs, verification tags, or preserved plaintext
   (Charter I.12); never disclose a phantom/decoy identity (I.11); expose **no detection knob** (only
   the budget — retention time + capacity — is tunable); calm by default, no scareware; absolute
   words ("verified", "byte-for-byte") scope to the per-file mechanism only (VI.3).
5. **Two planes enforced by TYPE** (FRONTEND_DESIGN XIII.4). Posture-plane DTOs carry no path and no
   free text (enum/numeric only); anything itemized/consequential goes over the elevated pipe.
6. **Exemption anchor stays content-hash ∧ certificate-subject** (X.3). NEVER relax to signer-only /
   min-version; no silent auto-heal on update.
7. **Verification cadence (owner's standing directive):** heavy verification (VM runs, interactive
   GUI owner pass) is **batched at the end**, not per-slice. Per-slice keep the cheap gates:
   `dotnet build` 0 errors + `dotnet test` green + a **local GUI smoke** (§6). Optional FABLE5
   code-review per substantial slice.
8. **FABLE5 review discipline (owner-directed, done twice this session):** present confirmed logic to
   FABLE5 **at code level**, demand code-level counterexamples + fixes (not prose), then **critically
   accept** — reject findings that don't reproduce against the *real* code, with evidence; never
   rubber-stamp, never ignore a valid finding. Frame the review as benign desktop MVVM (a
   file-backup utility) — avoid ransomware/attack/malware terms or FABLE5 refuses. See §4 for what
   the two reviews already found.

---

## 2. WHAT IS DONE THIS SESSION (committed to `main`, build 0 / test 109 / GUI-smoked)

Eight commits, newest last: `7c8375c` `51da5bd` `1f17623` `6678cbf` `92caca5` `2c554ce` `c011b7e`
(plus the pre-session `87f4018`). **Not pushed** — the operator controls push.

### 2.1 IA realigned to the mock's 3 surfaces (was 5)
`MainViewModel` now exposes **Home / Recovery / Budget & exemptions**. `PlaceholderViewModel` and
`PlaceholderView.*` deleted. Sidebar gained a **mode chip** (see 2.5).

### 2.2 Design-token SSOT — closes FRONTEND_DESIGN I.3
`frontend/design-tokens/ladder.tokens.json` is the single source (embedded into the App assembly,
logical name `SemanticsAr.App.ladder.tokens.json`). `SemanticsAr.App/Design/DesignTokens.cs` loads it
at startup and installs **posture** brushes (`Status.{Green,Amber,Red,Neutral}.Brush`) **and distinct
ladder** brushes (`Ladder.{definitive,bounded,unrecoverable}.Brush`). `CertaintyChip` reads
label + theme-reactive accent from the tokens. **§0.2 two-axis correction applied:** rung colors are
no longer wrongly reused from posture colors (BOUNDED rung amber ≠ posture-alarm amber). `ThemePalette`
was deleted. **A real startup bug was caught here by the GUI smoke** (`51da5bd`): `Tokens = Load()`
initialized before the `SerializerOptions` field → deserialized with null (case-sensitive) options →
empty dicts → `KeyNotFoundException`. Fixed by inlining the options in `Load()`.

### 2.3 Recovery destination model reconciled to the mock (§5.B of the old handoff)
Retired the per-item `_RESTORED_` sibling default. `RestorePlanner` now has pure, tested
`Decide(rung, disposition, destination)`, `FolderTargetPath(...)`, `DefaultFolderRoot(when)`. Preview
offers a **destination radio: "Restore to original locations" | "Copy to a folder" (`C:\Recovered\<date>\`)**,
modified-since unchecked by default. **Honest degradation (verified from `service/recovery.c`):**
`preserve_recover` passes a normal `C:\...` path through unchanged (only `\Device\...` NT paths get the
`\\.\GLOBALROOT` prefix), so folder-copy works for **BOUNDED**; **DEFINITIVE reads ciphertext from the
target path so it cannot be redirected** → in folder mode DEFINITIVE items are declined with an honest
reason (`RecoveryDeclineReason.DefinitiveFolderOnly`). III.6 (DEFINITIVE always in-place,
verify-before-replace) preserved. The `SemanticsAr.Verify` harness was updated to the folder round-trip.

### 2.4 Budget & exemptions surface — hero only (the rest is backend-gated, see §5)
`BudgetSnapshot.FromPreserve(...)` (pure, tested) derives the budget hero (achieved window, cache
bytes, oldest copy) from the **existing** `LoadPreserved` snapshot — no new backend. `BudgetSession`
mirrors `RecoverySession` (one elevation → snapshot → browse; IX.7). `BudgetViewModel`/`BudgetView`
render the hero. **The attribution ranked-list, the exemptions & trust log/revoke, quantified cost,
and budget-settings readback are NOT built — they are genuinely gated on the §5 backend projections.**
Building them now would fabricate data or invent un-mocked UX; do NOT.

### 2.5 Category E — Response/Policy plane (mode-switch + onboarding)
- **E.1 mode-switch** (`6678cbf`): the sidebar chip is now a Button gated on `CanSwitchMode` (only when
  mode is AUDIT/ENFORCE — correctly disabled when the service is unreachable). Click → `ModeSwitchWindow`
  (modal) with `ModeControlViewModel`: target = opposite of current; adopting ENFORCE states all three
  Constitution triggers verbatim (Charter VI.2.1); Confirm opens the elevated channel and calls
  `SetMode` (AUDIT=0 / ENFORCE=1 per `protocol.h`), reporting applied / unavailable / (UAC-cancel →
  stays on confirm). Reversible.
- **E.2 onboarding** (`92caca5`): a 4-step first-run wizard (`OnboardingWindow` + `OnboardingViewModel`)
  shown once over the main window: (1) pre-empt the driver/SmartScreen prompts, (2) two recovery assets
  + the coverage line in full (reuses `HomeViewModel.CoverageLine`), (3) **AUDIT as the honest default —
  ENFORCE's consequence is stated but NOT applied here (VI.4.1 nudges to ENFORCE only after quiet, so
  onboarding calls no SetMode and needs no elevation)**, (4) how to pin the tray glyph. Completion is
  persisted in the **registry** (`HKCU\Software\MetaForensics\SemanticsAr\OnboardingCompleted`), NOT a
  file — a `SemanticsAr*` file would trip the driver's self-protection write-deny (§6.4 gotcha).

### 2.6 Canonical failure wording (XI) — `2c554ce`
`SemanticsAr.Core.Domain.ElevatedErrorText.Describe(error, action)` is the single source for the
elevated-error family; the Recovery/Budget/Mode VMs delegate to it (removed a 3-way divergent
duplication). Tested in `ElevatedErrorTextTests`.

### 2.7 WinAppSDK deployment fixed — the app never needs a manual runtime patch (`1f17623`)
The app uses WinAppSDK 1.8 **only** for toast notifications (`AppNotificationManager`, V.3). Framework-
dependent, that made the runtime a hard startup dependency (the app failed to start without the exact
Windows App Runtime). **Decision (research + empirical publish/smoke, in `SemanticsAr.App.csproj`):**
`WindowsAppSDKSelfContained=true` + `RuntimeIdentifier=win-x64`, keeping `SelfContained=false`. Verified:
`Microsoft.WindowsAppRuntime.dll` is bundled, `coreclr.dll` is not — **WinAppSDK self-contained, .NET
framework-dependent** (IV.3 central-servicing preserved for the big runtime). A manual soft-bootstrap
was investigated and rejected (fights the framework: eager undocked-regfree init + broken native-DLL
copy; unverifiable runtime-absent path). **Consequence:** build output is now under
`bin/Release/net10.0-windows10.0.19041.0/win-x64/`; the installer (§5 Slice F) no longer needs a
Windows App Runtime chain-install — only the .NET 10 Desktop Runtime (IV.3).

### 2.8 FABLE5 hardening — `c011b7e` (adjudicated; see §4)
Onboarding-finish made resilient to a registry-write throw; posture-change handlers guarded against a
shutdown race (MainViewModel + the pre-existing HomeViewModel/RecoveryViewModel); redundant
`CanSwitchMode` notifications gated (with a correction — the naive fix would have blanked the initial
mode label).

---

## 3. FRONTEND FILE MAP (key files a successor edits)

- `SemanticsAr.Core/Domain/`: `RestorePlanner` (Decide/FolderTargetPath/Anchor/Classify),
  `BudgetSnapshot`, `RecoveryOutcome` (+ `RecoveryDeclineReason`), `ElevatedErrorText`, `CertaintyRung`,
  `RecoveryLadder` (wire parse — `PreserveEntrySize=552`, `CatalogEntrySize=576`), `ElevatedError`.
- `SemanticsAr.Core/Services/`: `RecoverySession`, `BudgetSession`, `PostureService`,
  `ElevatedControlChannel` (implements `IElevatedControlChannel`: LoadCatalog/LoadPreserved/Recover/
  SetMode/SetBudget), `Win32FileProbe`, `Native*Reader`, `ElevationMoniker`.
- `SemanticsAr.App/ViewModels/`: `MainViewModel` (surfaces + mode chip + `CreateModeControl`),
  `HomeViewModel`, `RecoveryViewModel`, `RecoverableItemViewModel`, `RecoveryOutcomeViewModel`,
  `BudgetViewModel`, `ModeControlViewModel`, `OnboardingViewModel`.
- `SemanticsAr.App/`: `App.xaml.cs` (startup + onboarding gate), `MainWindow.xaml(.cs)`,
  `ModeSwitchWindow.xaml(.cs)`, `OnboardingWindow.xaml(.cs)`, `OnboardingStore.cs` (registry),
  `Design/DesignTokens.cs`, `Controls/CertaintyChip.*`, `Converters/EnumToBooleanConverter.cs`,
  `TrayIconController.cs`, `Notifications/ToastNotifier.cs`.
- `frontend/design-tokens/ladder.tokens.json` — token SSOT (embedded).
- `SemanticsAr.Verify/Program.cs` — headless harness (posture + elevated recover round-trip).
- Tests: `SemanticsAr.Core.Tests/*` (109 passing) — App VMs are not unit-tested (they are
  GUI-smoked); if you add an App test project, `ModeControlViewModel`/`OnboardingViewModel` are pure
  enough to test.

---

## 4. THE TWO FABLE5 REVIEWS (already done — do not re-litigate; extend the discipline)

**Review 1 (A/0/B logic):** accepted — `Anchor` throwing on a corrupt near-max FILETIME under a
positive UTC offset (fixed with `FromFileTimeUtc`); `DefaultFolderRoot` culture-sensitive calendar
(fixed with `InvariantCulture`); a budget empty-state inconsistency + pluralization; a nullable-enum
`ConvertBack`; `FolderTargetPath` separator/surrogate edges; `Freeze()` on theme brushes. Rejected with
source evidence: several "unreachable in the real pipeline" claims.

**Review 2 (E logic):** accepted — F1 onboarding-finish resilience to a registry-write throw; F2 posture
shutdown-race guard (also fixed the same pre-existing pattern in Home/Recovery); F6 redundant-notify
(with a correction — kept ModeLabel unconditional so the initial `MODE UNKNOWN` still renders). Rejected
with evidence: "Dispose-after-success masks success" (the real `ElevatedControlChannel.Dispose` swallows,
cannot throw); "blanket catch hides fatal" (intentional broad-catch, matches RecoverySession/
BudgetSession); the low-severity cancel-requeue and the redundant Unknown-ctor guard.

**Takeaway for the successor:** FABLE5 finds real bugs AND overreaches; verify every finding against the
actual code before applying, and correct fixes that would regress (F6 would have blanked the label).

---

## 5. WHAT REMAINS — categories and their HARD gates (do not fabricate around these)

The cleanly frontend-only, no-backend scope is **done**. Everything below has a real external gate.
Do not invent data, un-mocked UX, or unverifiable wire changes to route around a gate.

### Slice D → C — backend projections, then Budget enrichment (highest product value: the mock's 2nd hero)
The Budget attribution list, exemptions & trust (log/revoke/staleness), quantified exempt cost, and
budget-settings readback all need backend projections that don't exist:
- **XII.1 per-item pool** and **XII.4 actor key** are *cheap*: `sar_preserve_record_t` already carries
  `state` (PROBATION=0/PROTECTED=1) and `actor_id`, but the `sarapi_preserve_entry_t` projection **drops
  them** (`frontend/sarapi/include/sarapi.h`). Widen the projection + `RecoveryLadder.ParsePreserve` +
  `PreserveEntrySize` in lockstep.
- **XII.2 per-copy app attribution** is genuinely new (record the causing app at preserve time;
  forward-accruing — file first). **XII.5 exemption-record enumerate** is a new service verb.
  **Status readback** (target retention / cache ceiling / pools / sparkline history) is new or partly
  derivable from the preserve snapshot.
- **Also surface the three existing COM verbs** `ResolveIdentity`/`WhitelistAdd`/`WhitelistRemove` through
  `IElevatedControlChannel` (they are on `ISarElevatedControl` but not the channel) — only once a consumer
  (the exempt flow) exists, else it is dead code.
- **GATE:** these are `sarapi_preserve_entry_t` **wire changes** → rebuild `sarapi.dll` (CMake) → rebuild
  and **redeploy the sealed VM backend** (`SarTarget`, snapshot `clean-baseline-20260704`) → governed
  Constitution-projection amendments (Const III.1.3). The backend SOURCE is in this repo
  (`driver/ service/ engine/ control/`) but the RUNNING backend is the sealed VM. **Needs the operator to
  enable the VM + approve the backend redeploy.**

### Slice F — installer / packaging / self-protection / operational DoD
WiX/Burn bundle that chain-installs the **.NET 10 Desktop Runtime** (IV.3; WinAppSDK is now
self-contained, so no longer a prerequisite — see 2.7), lays down the app, registers the elevation-host
COM server. **Self-protection (§6.4):** the driver denies user-mode creation of any file whose name
begins with `SemanticsAr` on the monitored volume — so `SemanticsAr.App.exe` /
`SemanticsArElevationHost.exe` cannot be installed after the driver loads; install pre-driver-load or on
an exempt path; write new files, never bulk-overwrite a monitored dir (phantom seeding, §6.4).
**GATE:** WiX toolset availability + an **Authenticode code-signing certificate** + self-protection /
driver-load-order testing on the VM.

### Slice G — the batched verification (owner's standing directive)
Interactive GUI owner feel-pass; full VM harness incl. the new folder round-trip and (when built) the
mode-switch/SetMode round-trip; XIII.4 no-path proof artifacts; XII wire checks; High Contrast; pseudo-
localization; idle-memory/list-scroll; crash-dump sensitivity (XI.2); each consequential verb under
classic UAC and (when testable) Administrator Protection. **GATE:** the `SarTarget` VM.

### Frontend-only work still available WITHOUT a gate (if the operator prefers to defer the gated work)
Accessibility / keyboard / High-Contrast local pass; an App-VM unit-test project; surfacing the
remaining XI failure states that are already representable (elevation-declined is handled as cancel;
integrity-halt needs the XII.3 backend signal — gated). Visual polish to mock fidelity is owner-gated
(XIII.6), not to be finalized unilaterally.

---

## 6. HOW TO BUILD, TEST, AND GUI-SMOKE (the GUI now runs — use this every change)

Machine: .NET 10.0.301 SDK, CMake, VS 2022, Hyper-V. VM `SarTarget` runs the sealed backend.

- **Build:** `dotnet build frontend/SemanticsAr.App/SemanticsAr.App.csproj -c Release` → exe at
  `frontend/SemanticsAr.App/bin/Release/net10.0-windows10.0.19041.0/win-x64/SemanticsAr.App.exe`
  (note the **win-x64 subfolder** — from the RID in 2.7). `sarapi.dll` is copied next to it if built.
- **Test:** `dotnet test frontend/SemanticsAr.Core.Tests/SemanticsAr.Core.Tests.csproj -c Release`
  (currently **109/109**). Warnings `MVVMTK0045` are harmless WPF advisories.
- **GUI smoke (no backend needed — catches XAML/startup/binding-crash class):** launch the exe via
  PowerShell `Start-Process ... -PassThru`, wait ~7s, inspect `MainWindowTitle`:
  `semantics-ar` = started; `... This application could not be started` = a runtime/bootstrap failure;
  early exit with code `0xE0434352` = an unhandled managed exception (redirect stderr / read the
  `.NET Runtime` Application event-log entry to get the stack). Drive nav / dialogs with
  `System.Windows.Automation` (UIA): find the nav `List`, select items by index; find buttons by Name and
  Invoke. **Onboarding:** it shows once; to re-trigger, delete
  `HKCU:\Software\MetaForensics\SemanticsAr\OnboardingCompleted`. The **mode chip is disabled without a
  running service** (mode reads Unknown) — that is correct, not a bug.
- **Self-contained smoke (dependency-free window):** if a raw build won't start due to a missing runtime,
  `dotnet publish ... -r win-x64 --self-contained false -p:WindowsAppSDKSelfContained=true -o <tmp>` and
  run that (bundles WinAppSDK). This is how the WinAppSDK decision was verified.
- **VM ceremony (batched, when unblocked):** see the prior handoff / `scripts/vm_verify_new.ps1`; deploy
  the harness + host under **neutral names** (§6.4) as individual files; `RegServer` the host; run
  `sarverify.exe`.

## 7. GOTCHAS (carry forward)
- **`SemanticsAr*` file write-deny** on the monitored volume (§6.4) → onboarding flag is in the
  registry; the installer must install pre-driver-load or on an exempt path, writing individual new
  files (never bulk-overwrite / `Expand-Archive -Force` into a monitored dir — phantom seeding).
- **WinAppSDK is self-contained + RID-pinned** (2.7) → build output moved to the `win-x64` subfolder;
  a plain build errors without the RID (`WindowsAppSDKSelfContained requires a supported Windows
  architecture`).
- **Two color axes** must never merge: posture verdict (green/amber/red) vs certainty rung
  (def-teal/bnd-amber/unr-grey). BOUNDED-rung-amber is descriptive, NOT a posture alarm.
- **The mode chip / consequential verbs need a running backend** to exercise interactively (SetMode,
  recover round-trip) — that is the batched VM pass.
- **`runas /trustlevel` is not a clean medium-IL test** (SAFER token reports HIGH IL). Use a real
  standard user / linked token.
- The backend source is in-repo but the **running** backend is the sealed VM snapshot — a wire change
  is unverified until the VM is rebuilt/redeployed.

## 8. DEFINITION OF DONE (per slice)
Charter Part XI + Constitution Part XI + FRONTEND_DESIGN XIV, **and** the cheap gates every slice
(build 0 / test green / GUI smoke), **and** — at the batched end — VM-verified against the running
backend and visually owner-passed. Prove, don't assert: the no-path / no-prompt / verify-before-replace
guarantees are test artifacts (`SemanticsAr.Verify` + the XIII.4 schema/negative tests), not review
claims.
