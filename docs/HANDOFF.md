# semantics-ar — FRONTEND HANDOFF (2026-07-10, rewrite)

> **Read this first, then the governing docs.** This is a full reset of the prior handoff. It
> reflects the state after the **elevated Recovery vertical was hardened and VM-verified** and,
> critically, after the **owner-approved mock was reconciled against the code** and found to
> define a **different information architecture** than the prose docs. A successor picking this up
> cold must internalize §2 (what the target actually is) before writing any UI.

## 0. GOVERNING DOCUMENTS — precedence order (obey; amend there first, never silently substitute)

1. `docs/CONSTITUTION.md` — what the system proves/does (RATIFIED, authoritative).
2. `docs/EXPERIENCE_CHARTER.md` — how it is presented to a human (RATIFIED, subordinate).
3. `docs/FRONTEND_DESIGN.md` — the frontend spine, v1.1 RATIFIED (subordinate to both).
4. **`frontend/mocks/recovery-and-budget.v1.html` — the owner-approved VISUAL/IA target.** Where
   the mock and FRONTEND_DESIGN's prose disagree on **information architecture or a hero-flow
   interaction**, **the mock wins** (HANDOFF binding rule 6; it is "the visual target"). Open it,
   read `frontend/mocks/README.md`, and toggle its "Show data provenance" overlay — every value
   traces to a wire field or a **PRECONDITION** (amber) tag naming an unshipped backend field.

## 1. BINDING RULES (non-negotiable)

1. **Code has NO comments.** Match the surrounding code's idiom.
2. **No dead / compatibility / fallback / migration / schema-version code.** The project has never
   shipped. If you find such traces, a past implementer erred — **remove** them.
3. **Reuse only what genuinely fits the ratified design + the mock.** Verify against the current
   wire (`frontend/sarapi/include/sarapi.h`, `common/include/semantics_ar/*.h`) **and the mock**
   before trusting any inherited file. The frontend was built *after* the Constitution's "rewrite
   as-if-original" (git `7a28d45`) and against the current backend (elevated Recovery was VM 19/0
   at git `1a5e70a`), so it is **not legacy-coupled** — but it *is* mock-misaligned on IA (§2).
4. **Honesty guardrails:** never surface key material, IVs, verification tags, or preserved
   plaintext (Charter I.12); never disclose a phantom/decoy identity (I.11); expose **no detection
   knob** — the only user-tunable value is the budget (retention time + capacity); calm by default,
   no scareware; absolute words ("verified", "byte-for-byte") scope to the per-file mechanism.
5. **The two-plane split is enforced by TYPE** (FRONTEND_DESIGN XIII.4). Posture-plane DTOs carry
   no path and no free text (enum/numeric only); anything itemized/consequential goes over the
   elevated pipe. Proven by the `sarapi` boundary + `SemanticsAr.Verify` artifacts.
6. **The exemption anchor stays content-hash ∧ certificate-subject** (FRONTEND_DESIGN X.3). NEVER
   relax to signer-only / min-version; no silent auto-heal on update.
7. **Verification cadence (owner's standing directive):** heavy verification (VM runs, interactive
   GUI) is **batched at the end of implementation**, not per-slice. Per-slice keep only the cheap
   gates: `dotnet build` 0 errors + `dotnet test` green. Design + optional FABLE5 code-review per
   slice is fine; the comprehensive review/VM/GUI pass is one end-of-implementation batch.

---

## 2. THE TARGET IS 3 SURFACES (mock), NOT 5 (prose). Internalize this.

`FRONTEND_DESIGN` Part VII describes **five** surfaces (Home, Recovery, Activity, Response/Policy,
Settings). **The owner-approved mock has three nav items:**

- **Home** — posture hero: verdict, DEFINITIVE/BOUNDED counts, recovery-window health, **copy pools
  (protected/probation)**, component health, coverage line, a "window shorter than target" amber
  notice → deep-links to Budget, and a "most recent incident" card → deep-links to Recovery.
- **Recovery** — certainty-ladder legend; **incident cards** (expandable, per-item, ladder-ranked);
  a **flat BOUNDED list** for copies not tied to an incident; a healthy empty-state. Preview modal
  and Report modal (see §4).
- **Budget & exemptions** — budget hero (achieved-window vs target, sparkline, cache-%); **ranked
  per-app attribution list** (share / time-impact / bytes, expandable to file-type breakdown, an
  "Exempt this app" button per row); **"Exemptions & trust"** (staleness re-affirm cards +
  exemption log with revoke); **"Budget settings"** (target-window stepper + cache-ceiling stepper).

**The mock FOLDS:** Settings → "Budget settings" inside Budget; exemption management → "Exemptions
& trust" inside Budget; Activity/Detections → incidents inside Recovery + Home's "recent incident"
(no separate Activity nav). **Mode switching UI and Onboarding are OUT of this mock's scope** — the
mock shows mode as a read-only sidebar chip only; those flows exist in the prose docs
(FRONTEND_DESIGN VI.2 ENFORCE friction, VIII.5 onboarding) but are **not yet visually mocked**, so
design them from the docs when reached and keep them consistent with the mock's language.

> **Consequence for the current code:** `frontend/SemanticsAr.App/ViewModels/MainViewModel.cs`
> currently wires **5 surfaces** (Home, Recovery, and three `PlaceholderViewModel`s named Activity
> / Response / Settings). **This is wrong per the mock.** The first realignment task (§5.A) is to
> restructure it to the 3 mock surfaces and delete the placeholders (`PlaceholderViewModel`,
> `PlaceholderView.*`).

---

## 3. WHAT IS DONE (VM-verified) AND WHERE IT STANDS

### 3.1 Posture plane (Slice 1) — DONE, VM 9/0
`sarapi.dll`; `SemanticsAr.Core` (`NativePostureReader`, `PostureService`, `PostureEvaluator`,
`JournalService`/`NativeEventReader`); `SemanticsAr.App` Home + tray + `ToastNotifier`. 40-byte
no-path posture frame; redacted event stream; III.7 server-is-SYSTEM check passes.

### 3.2 Elevated itemized plane + Recovery vertical — DONE, VM 13/0 (twice, this session)
The elevated transport and the Recovery hero **mechanism** are built, unit-tested (86/86), and
**VM-verified 13/0** against the live backend in `SarTarget` (activation → host→service
server-is-SYSTEM → snapshot of 1748 real items → classification → single-use host exit → recover
round-trip). Changes made this session (all build-green):

- **B1** — `ElevationMoniker.Activate` now requests `IID_ISarElevatedControl` directly and pins the
  proxy to `RPC_C_IMP_LEVEL_IDENTIFY` via `CoSetProxyBlanket` (the blanket must sit on the
  interface proxy, not IUnknown), keeping the UAC-cancel translation. Closes FRONTEND_DESIGN
  III.7(b). (`ElevationMoniker.cs`, `NativeMethods.Com.cs`.)
- **B2** — elevation host registers `REGCLS_SINGLEUSE` (was MULTIPLEUSE): serves exactly the one
  activation that spawned it, then exits → no unconsented re-bind by a same-session process.
  (`elevation-host/src/host_main.cpp`.)
- **B3** — `RecoverySession` releases the elevated channel **after** the snapshot (host exits) and
  **re-elevates per mutating action** (III.1/III.3); reentrancy guarded by a UI-thread `_busy`
  flag on `Begin`/`Confirm`/`Close`. (`RecoverySession.cs`, `RecoveryViewModel.cs`.)
- **B4** — `RestorePlanner.Classify` classifies each item (Additive / DeletedSince / ModifiedSince /
  Blocked) via `Win32FileProbe`, which stats the provenance path through the **`\\?\GLOBALROOT`**
  bridge (see §6 path contract). Nullable FILETIME anchor guard; reparse→ModifiedSince; access-
  denied→conservative ModifiedSince. (`RestorePlanner.cs`, `Win32FileProbe.cs`.)
- **B5** — `RestorePlanner.SideBySidePath` builds a `_RESTORED_<stamp>` sibling with a 259-char
  budget + collision uniquify. **NOTE: this per-item sibling model DIVERGES from the mock — see
  §5.B; it must be reconciled to the mock's destination model.**
- **B8** — `RecoverableItem : IIncidentSource`; Recovery groups DEFINITIVE by the existing
  `IncidentGrouper` (actor-key, `TimeSpan.MaxValue`) and keeps every `ActorStartKey==0` item
  (all BOUNDED) as a flat list — **no item is ever dropped** (partition is purely by actor key).
- **In-place-only-on-opt-in invariant** (III.6 absolute): a restore writes **in-place only** when
  `Rung==Definitive` (Oracle decrypts ciphertext at the original path — verify-before-replace
  guards it), OR the item is `DeletedSince` (no file to destroy), OR the user explicitly ticks
  "Replace my current file". Everything else defaults to side-by-side. So mtime-classification
  accuracy is never safety-load-bearing. **DEFINITIVE is always in-place** (the recover verb reads
  ciphertext from the target path; a side-by-side target has no ciphertext → decline). This was
  the VM-surfaced fix (kernel decline `-8`/`-5` on a DEFINITIVE side-by-side).
- **recover-all** — `SelectAll`/`ClearSelection` commands added to Recovery browsing (VI.1.2).
- **Harness** — `SemanticsAr.Verify/Program.cs` gained a Slice-2 elevated section (activate →
  classify → single-use-host-exit → recover round-trip) that runs headless in the VM ceremony.

### 3.3 NOT done / gated / never run
- **The actual WPF GUI has never been run interactively.** All verification is the headless Core
  seam + the harness. The owner visual pass (consent rhythm, modals, tray) is a batched end task.
- **IA is still 5-surface in code** (§2) — realign first.
- **Recovery destination model** diverges from the mock (§5.B).
- **Budget & exemptions surface** — not started.
- **Mode-switch flow, Onboarding** — not started (not in the mock; design from docs).
- **Backend projection amendments** (§6.3) — not started.
- **Installer / packaging, operational DoD tier** — not started.

---

## 4. RECOVERY & THE MOCK'S HERO FLOW (read the mock, then this)

The mock's Recovery preview (`#ov-preview`) is the target interaction:
- **Two destination modes (radio):** "Restore to original locations" (in-place, verify-before-
  replace; **default**) OR "Copy to a folder instead" (`C:\Recovered\<date>\` — originals never
  touched).
- **Two item groups:** "Unchanged since the incident" (checked) and "Modified since the incident"
  (**unchecked by default**, "Restoring discards newer content — left unchecked for you").
- **Report modal:** "X of Y restored, byte-for-byte verified · Z declined (reason)", per-item
  VERIFIED/DECLINED tags, "kept copies unchanged", a recovery-log id.

The current code implements DEFINITIVE-in-place + modified-unchecked correctly, but expresses
"keep both" as **per-item `_RESTORED_` siblings** (B5) rather than the mock's **destination-mode
radio + whole-batch recovery folder**. Reconcile to the mock (§5.B).

---

## 5. NEXT WORK, PRIORITIZED (realign to the mock first)

### A. Realign IA to the mock's 3 surfaces — DO FIRST, clear-cut
Restructure `MainViewModel` to Home / Recovery / **Budget & exemptions**; delete
`PlaceholderViewModel.cs`, `Views/PlaceholderView.*`, `ViewModels/PlaceholderViewModel` wiring.
No backend dependency. Build-verify.

### B. Reconcile Recovery destination model to the mock — needs care (touches VM-verified code)
Replace the per-item `_RESTORED_` sibling default with the mock's model: a destination radio
(**Original locations** | **Copy to a folder** `C:\Recovered\<date>\`) plus modified-unchecked.
Keep the in-place-only-on-opt-in invariant and DEFINITIVE-always-in-place. **Backend limitation:**
"Copy to a folder" needs the recover verb to read ciphertext from the original and write plaintext
to a *different* path; the current verb uses one path for read+write, so **folder-copy works for
BOUNDED (store→folder) but not DEFINITIVE**. Until a `recover-to-alternate-dest` verb exists
(§6.3), honestly degrade: folder-copy applies to BOUNDED; DEFINITIVE is original-locations only.
`RestorePlanner.SideBySidePath` can be retired or repurposed for the folder-path builder.

### C. Build the **Budget & exemptions** surface (the mock's second hero)
Budget hero + ranked attribution list + Exemptions & trust + Budget settings. Verbs that already
exist on the elevated pipe: `SetBudget`, `SetMode`, `ResolveIdentity`, `WhitelistAdd/Remove`
(the last three are on `ISarElevatedControl` but **not yet surfaced through**
`IElevatedControlChannel` — add them). Exempt confirm modal (resolved identity inline + consequence
in ladder terms + quantified cost), interpreter hard-stop refuse modal, changed-publisher warning,
staleness re-affirm. **Honest degradation** where backend fields are missing (§6.3): app attribution
(XII.2), per-item pool (XII.1), preserve actor key (XII.4), **exemption-record read (XII.5)**,
quantified cost (needs XII.2). The mock's provenance overlay shows exactly which values are
PRECONDITION-gated.

### D. Backend projection amendments (§6.3) — governed, then their dependent frontend features.
### E. Mode-switch flow + Onboarding (from docs; not mocked).
### F. Installer + Authenticode(app) + the `SemanticsAr*` self-protection handling (§6.4) + operational DoD tier (XIV).
### G. THE batched verification: interactive GUI visual pass + full VM harness + XII wire checks + comprehensive review → fix.

---

## 6. GROUND TRUTH (facts established by source-reading + VM this session)

### 6.1 The wire (three pipes, protocol version 1)
Headers: `common/include/semantics_ar/{protocol.h,posture.h}`, `frontend/sarapi/include/sarapi.h`.
Pipes: posture (`\\.\pipe\SemanticsAr.Posture`, session-readable, no path), events (redacted),
control (elevated, SYSTEM+Admins, ECDSA handshake). Identity verdict enum (`service/identity.h`):
`VERIFIED=0, UNSIGNED=1, HASH_FAILED=2, PATH_FAILED=3, ERROR=4`.

### 6.2 Provenance path contract (confirmed from `driver/capture.c`, `service/recovery.c`, `engine`)
Catalog/preserve provenance and the recover `target_path` are **NT device paths**
(`\Device\HarddiskVolumeN\...`, from `FltGetFileNameInformation(FLT_FILE_NAME_NORMALIZED)`). To
open one from user mode (client stat, or the service's atomic replace), prefix **`\\?\GLOBALROOT`**
(the client uses this; the service's own code uses `\\.\GLOBALROOT` — use `\\?\` in .NET: it is
verbatim, avoids trailing-dot mangling and MAX_PATH). Oracle (DEFINITIVE) recovery **reads the
ciphertext from `target_path`**, decrypts, `sar_recover_verify` recomputes the capture-time sample
tag and **declines on mismatch** (verify-before-replace), then replaces atomically → DEFINITIVE is
inherently in-place.

### 6.3 Backend projection amendments the frontend needs (governed, Const III.1.3 discipline)
- **XII.1** per-item pool status on `preserve_entry` (protected/probation) — BOUNDED firm-date honesty.
- **XII.2** per-copy app attribution on preserve holds (**forward-accruing; file early**) — Budget attribution + exempt quantified cost.
- **XII.4** `actor_start_key` on `preserve_entry` — BOUNDED incident grouping (else flat list).
- **XII.5 (NEW this session)** exemption-record **read** projection — the protocol has whitelist
  add/remove/resolve but **no way to enumerate current exemptions** or their version/hash/signer
  diffs; the mock's exemption log + staleness re-affirm need it.
- **recover-to-alternate-dest verb (NEW this session)** — for the mock's "Copy to a folder" on
  DEFINITIVE items (read ciphertext from original, write plaintext elsewhere).

### 6.4 GOTCHAS
- **`SemanticsAr*` self-protection write-deny.** The driver denies user-mode creation of any file
  whose name begins with `SemanticsAr` (CamelCase) on the monitored volume, anywhere
  (`semantics_ar_*` and neutral names pass). → The product's own frontend binaries
  (`SemanticsAr.App.exe`, `SemanticsArElevationHost.exe`) cannot be installed on a monitored volume
  **after** the driver loads. Packaging must install pre-driver-load or on an exempt path. For VM
  tests, deploy the host under a neutral name (RegServer records whatever path); the `.tlb` name is
  hardcoded in `elevation-host/src/registration.cpp` (`kTlbFile`) — if you must deploy it, rename
  there too (it was reverted to canonical this session).
- **Phantom seeding** (Const VIII): creating a new dir under a monitored volume plants protected
  decoys; a non-exempt bulk delete/overwrite there is denied. Deploy **individual new files**, never
  `Expand-Archive -Force` into a monitored dir.
- **`BLOCK_INJECTION` (event class 8) floods** and is silent by design (no toast string).
- **`runas /trustlevel` is not a clean medium-IL test** (SAFER token reports HIGH IL → III.7 false
  negative). Use a real standard user / interactive / linked token.
- **MVVMTK0045** WinRT-AOT advisories on `[ObservableProperty]` are harmless for WPF.

---

## 7. HOW TO BUILD & VM-VERIFY

Machine has .NET 10 SDK, CMake, VS 2022, Hyper-V. VM **`SarTarget`** runs the sealed backend
(snapshot `clean-baseline-20260704`). CMake tree already configured at `build_fe/`.

- **sarapi.dll:** `cmake --build build_fe --target sarapi --config Release` → `build_fe/frontend/sarapi/Release/sarapi.dll`.
- **elevation host:** `cmake --build build_fe --target sar_elevation_host --config Release` → `build_fe/frontend/elevation-host/Release/SemanticsArElevationHost.exe` (+ `.../sar_elevation_host.dir/Release/SemanticsArElevation.tlb`).
- **frontend:** `dotnet build frontend/SemanticsAr.App/SemanticsAr.App.csproj -c Release`. Tests: `dotnet test frontend/SemanticsAr.Core.Tests/SemanticsAr.Core.Tests.csproj -c Release` (currently **86/86**).
- **verify harness:** `dotnet publish frontend/SemanticsAr.Verify/SemanticsAr.Verify.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true` → `.../publish/sarverify.exe` + `sarapi.dll`.
- **VM ceremony (this session, works):** `New-PSSession -VMName SarTarget -Credential (admin/admin)`
  (runs High-IL admin → elevation moniker activates without a prompt). Ensure `semantics_ar_service`
  running; `C:\sar\sarctl.exe mode audit` + `... budget 604800 10240`. Deploy `sarverify.exe`,
  `sarapi.dll`, and the host+tlb **under neutral names** (`sar_elevation_host.exe`,
  `sar_elevation.tlb`) as individual files (Copy-Item -ToSession). `Start-Process
  sar_elevation_host.exe RegServer -Wait` (registers CLSID/AppID/Elevation/Interface/TypeLib). Run
  `sarverify.exe`; expect **13/0**. `scripts/vm_verify_new.ps1` is the full backend deploy ceremony
  (reuse its Connect-VM / CopyToVM conventions).

## 8. DEFINITION OF DONE (per slice)
Charter Part XI + Constitution Part XI + FRONTEND_DESIGN XIV conformance, **and** — at the batched
end — VM-verified against the running backend and visually owner-passed. Prove, don't assert: the
no-path / no-prompt / verify-before-replace guarantees are test artifacts (`SemanticsAr.Verify`
+ the XIII.4 schema/negative tests), not review claims.
