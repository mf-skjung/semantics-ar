# semantics-ar — Demo Attack Runbook

> **TEST MODE — this is a test-signed build on a throwaway demo VM. Not production.**

A four-act story that shows the product catching and recovering from a ransomware-shaped
attack. Everything runs inside the walled-off sandbox `C:\SarDemo\Sandbox`; the attack
cannot touch anything else (fixed root, canary, manifest — there is no way to aim it
elsewhere).

Before you start: `DEMO READY` screen shown, the app is open, and the sandbox holds a dozen
sample documents. If not, run `Start-SarDemo.ps1` first.

---

## Act 1 — Baseline (≈10s)
- Show the app: **minifilter attached, service RUNNING**. Point at the Home verdict seal.
- Open `C:\SarDemo\Sandbox` and open one document so the audience sees real content.
- Line: *"This is a user's Documents folder. The product is watching every write."*

## Act 2 — ENFORCE blocks at the first instance
- In the app, make sure the mode is **ENFORCE** (or run `sarctl mode enforce` if available).
- Run **`Demo-Attack.cmd`**.
- Narrate: *"A ransomware process starts encrypting files in place."*
- The script reports **every overwrite was blocked**. The app raises a detection and names
  the offending process; the files are intact.
- Re-open a "targeted" document → still readable.
- Line: *"It didn't need to lose a hundred files to learn. It stopped instance one."*

## Act 3 — AUDIT records, then recovery brings files back
- Switch the app to **AUDIT** mode. Explain: *"Audit mode intentionally does **not** block —
  it records and keeps files recoverable, so you can see the forensic capture."*
- Run **`Demo-Attack.cmd`** again. This time files get encrypted (the ransom note appears).
- In the app, show the recorded event trail + the app that caused it (attribution), then use
  **Recovery** to restore the files from the preserved pre-images.
- Open a restored document → original content is back.
- Line: *"Even when we let it run, nothing is lost — every change was preserved and reversed."*

## Act 4 — Reset for the next audience (≈5s)
- Run **`Reset-Attack.ps1`** → the sample files are re-seeded to their originals.
- **Switch the app back to ENFORCE** so the VM is left in the protective posture.

---

### Notes for the presenter
- AUDIT letting the attack through is **by design**, not a product failure — say so, or the
  audience may misread it.
- To fully reset the VM afterwards: `Reset-SarDemo.ps1` (uninstalls the product, turns test
  signing off, one reboot).
- The attack makes **no network calls, sets no persistence**, and touches **only** the
  sandbox. It is a demonstration primitive, not malware.
