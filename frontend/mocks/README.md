# frontend/mocks

Design prototypes for the T4 operator-surface rebuild. **These are not shipping code** —
they are contract-aware HTML mocks used to settle information architecture, wording, and the
visual language with the product owner *before* WPF implementation (per
`docs/FRONTEND_DESIGN.md` §XIII: mock by uncertainty, wire by risk).

## recovery-and-budget.v1.html

A single self-contained interactive prototype of the two highest-uncertainty surfaces:

- **Recovery** (`docs/FRONTEND_DESIGN.md` Part VIII) — incident-organized, ladder-ranked,
  with the flagship preview → verified-report flow and the "modified since the incident"
  safety split (modified-since items unchecked by default).
- **Recovery Budget & Exemptions** (Part IX / X) — time-denominated budget, per-app
  attribution ranked list, the honest exemption confirm, the calm staleness re-affirm, the
  changed-publisher warning, and the interpreter hard-stop.
- Plus a compact **Home** posture hero and the shared **certainty-ladder** signature
  component.

### How to view
Open the file in a browser. It is fully self-contained (inline CSS/JS, inline SVG, system
fonts, no external requests). It was also published as a Claude Design artifact:
`https://claude.ai/code/artifact/afbef6e6-cf0c-45d8-8088-00439671d6c0`.

### Two controls worth knowing
- **"Show data provenance"** (top bar) overlays, on every value, the backend field it traces
  to — the field-ledger self-audit required by §XIII.2. **Amber `PRECONDITION` tags** mark
  values that depend on a not-yet-shipped backend field (Part XII): per-item pool status,
  the preserve actor key, and per-copy app attribution. The mock shows the honest fallback
  wording used until those land (e.g. all live BOUNDED items use probation-honest wording;
  the "older activity (before per-app attribution)" bucket is first-class).
- **Theme toggle** (top bar) — light/dark, also honoring the OS preference.

### Conformance notes / known reconciliation items
- Verified against the honesty guardrails: no key material / IV / verification tag /
  preserved plaintext is ever shown; no decoy identity; no detection knob (only the budget is
  tunable); every rung carries color + icon + label; calm-by-default, no scareware.
- **Mode label:** the mock says "PROTECT mode" — a deliberately neutral placeholder. The real
  product modes are **AUDIT / ENFORCE** (Const. Part V); reconcile the label when the mock is
  next revised.
- Data is illustrative but obeys the field ledger — no field is shown that does not trace to
  a real wire field (`common/include/semantics_ar/*.h`) or a named Part XII precondition.

### Provenance
Derived from `docs/FRONTEND_DESIGN.md` v1.1 (RATIFIED), which is subordinate to
`docs/EXPERIENCE_CHARTER.md` and `docs/CONSTITUTION.md`.
