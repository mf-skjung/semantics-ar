# Handoff — semantics-ar Windows realization — Two-asset model (Oracle + Preservation) implemented (2026-06-26)

This is the single living handoff artifact for the chain that builds the Windows product
against the clean Constitution. It supersedes and consolidates the scattered "Deferred (later
slices)" / "Named deferrals" sections of `SLICE1_DESIGN.md`, `SLICE2_DESIGN.md`, and
`SLICE3_DESIGN.md`. Read it after the Constitution and the four slice design docs. Each
implementer updates it; the terminal implementer deletes it.

> Do not restate what the code or the slice design docs already show. This captures only the
> chain state, the remaining units, and the traps.

---

## 0. ACTIVE HANDOFF — passive capture-at-open (COO) landed; three units remain (read this first)

### 0.0 The constitutional trap that MUST NOT be repeated (read before touching the capture path)

**Constitution IV.3.1 is inviolable:** *"The gate emits capture-candidate-or-skip … it never blocks or
alters an operation; the write proceeds unchanged … it cannot corrupt a write, because it never redirects
one."* Recovery is **passive capture** (Oracle key capture III.1.2 + preservation staging III.5); the only
active response (STATUS_ACCESS_DENIED) is **strictly downstream of a verdict** (Part V). Before ANY
capture-path change, re-read IV.3, III.1.2, III.5, Part V.

A prior implementer (this chain) tried to close the non-cached / allocation-shrink gaps with
**redirect-on-write** (Redemption RAID'17 model: divert the attacker's writes to a reflected/shadow sparse
file, commit-on-acquittal / discard-on-conviction). It was built, wired, and VM-run — then **fully reverted**
because it violates IV.3.1: the gate would *intervene* (redirect) on every candidate open, including benign
ones, which the passive design exists to prevent (it produced a real Phase-C2 false positive when the verdict
was made at the gate). **DO NOT reintroduce redirect / shadow / reflected-file / copy-on-write-redirection /
transactional-commit.** Redemption is explicitly NOT our model (see §2 "we use a cryptographic tag"); we
share only verify-before-replace.

### 0.1 What landed this session (UNCOMMITTED, on top of 5f49e43 — do not commit until §0.2 is closed)

**Capture moved from the destructive WRITE to the write-intent OPEN (COO).** Root cause of the old gap: the
old path read the pre-image *at* the destructive write (a synchronous same-stream read in the write pre-op),
which self-deadlocks for non-cached/paging writes (confirmed live-KDNET stack: the read re-enters the NTFS
paging resource the writer already holds), so non-cached was `!IRP_NOCACHE`-gated → never preserved. The
constitutional fix is to capture at the moment the destroyer *opens* the file (before any modification):

- **`IRP_MJ_CREATE` post-op (`operations.c SarPostCreate`)** — for an existing user file (`FILE_OPENED`)
  opened with write/delete intent by a non-exempt user-mode process (dedup via new stream flag
  `SAR_STREAMCTX_FLAG_BASELINE_STAGED`), call `SarCaptureSubmitOpenBaseline` (`capture.c`), which creates a
  **data-scan section** (`FltCreateSectionForDataScan` — the official `avscan` pattern; requires
  `FltRegisterForDataScan` per instance, added in `driver.c SarInstanceSetup`, and a registered
  `FLT_SECTION_CONTEXT`, added to `g_sar_contexts`), maps it, copies the whole-file pre-image, stages it via
  `SarPreserveStage` (path-keyed, into the existing preservation store), then `FltCloseSectionForDataScan`.
  The user's write proceeds byte-for-byte unchanged — passive (IV.3-compliant). **Why the section and not a
  plain read:** the data-scan section reads the file's data regardless of the attacker handle's granted
  access (write-only / blind-wiper opens) or share mode (share-none); a plain `FltReadFile` on the attacker's
  file object fails for write-only opens, and an independent `FltCreateFileEx` fails for share-none opens —
  both VM-confirmed to drop modes. The section is the only mechanism that covers all of them.
- **Removed (subsumed by COO → no dead/duplicate capture):** the write-pre-op pre-image read block in
  `SarCaptureSubmitWrite` (the Oracle key-capture stays — it uses the 256B `read_sample` + `written`); the
  mmap section-acquire baseline (`SarPreAcquireForSection` no longer calls it — the destroyer must open the
  file writable before mapping, so COO subsumes mmap); the `SubmitDestruction` staging in
  `SarPreSetInformation` (delete/EOF/alloc) and `SarPreFsControl` (zero/trim/dup/offload); and the now-dead
  helpers `SarCaptureSubmitDestruction`, `SarStreamWasRead`, `SarQueryEndOfFile`, `SarDestructionRegion`.
  **Kept:** `SarCaptureSubmitRenameTarget` (rename-over/hardlink-replace destroy a victim that is never
  *opened* by the attacker, so it needs its own destination capture) and the metadata coverage seam.
- Files: `capture.c`, `capture.h`, `operations.c`, `seam.h` (`MAPPED_CAPTURED`→`BASELINE_STAGED`),
  `driver.c`, `driver.h` (`SAR_POOL_TAG_SECTION`). Builds clean. Net −218 lines.

**VM-verified (`vm_verify_coverage.ps1 -AttackCount 6`):** **non-cached 0→100 %, allocation-shrink 0→100 %**
(the target gaps), full destruction-member matrix **100 %** (disposeex/setsparse/trim/linkreplace/mmapclose/
intermittent/preoverwrite), Phase-A newdelete/renameover/setzero/mmap **100 %**, in-place encryption **100 %**,
**benign false-positives 0** (AUDIT + ENFORCE, incl. high-novelty corpus — because COO is passive copy, not
intervention), system stable. Recovery is checked through the normal `sarctl preserve-recover` path (COO
populates the preservation store, so the existing harness is the correct verifier — no methodology change).

### 0.2 Remaining units (close these, re-verify, THEN commit the whole thing; then production)

1. **truncate-on-open — the only remaining destruction gap (Phase-A `truncate` = 0 %).** That mode opens the
   victim with an overwrite/truncate disposition (`FILE_OVERWRITTEN`/`FILE_SUPERSEDED`), so the file is
   already 0 bytes when the create *completes* → post-create COO reads 0 → nothing staged
   (`preserve-recover result=-5`). This is the create-time-overwrite case (a blind wiper; note real
   encryption ransomware never truncates-on-open, it must read the plaintext first). **Fix: capture at
   PRE-create for `FILE_OVERWRITE`/`FILE_OVERWRITE_IF`/`FILE_SUPERSEDE` on an existing file** — at pre-create
   the file is still intact AND the attacker's handle is not yet established (no share conflict), so an
   independent `FltCreateFileEx(FILE_OPEN, FILE_READ_DATA, share-all)` opens it cleanly. Consider unifying:
   capturing *all* write-intent creates at pre-create via an independent handle would be one mechanism that
   also handles write-only/share-none without the data-scan section — but validate re-entrancy first
   (`FltCreateFileEx` at pre-create: guard `KeGetCurrentIrql()==PASSIVE_LEVEL` and `IoGetTopLevelIrp()==NULL`,
   target below own instance so no callback recursion). Trap: pre-create NAME resolution (file not open yet;
   `FLT_FILE_NAME_OPENED` fails — use `FLT_FILE_NAME_QUERY_DEFAULT`; watch `SL_OPEN_TARGET_DIRECTORY`; see the
   "Of Filesystems And Other Demons" pre-create-name posts). Still passive → IV.3-compliant.

2. **Gap C — phantom decoy identity consistency (Phase-P: metamorphic `tells=3`, open-by-file-id 3-A
   `ok=0/3`, `err=87`).** This is the Constitution VIII decoy layer (fabricates responses for *non-existent*
   decoys — orthogonal to IV.3, not gate redirection). Two defects, one root cause: identity is generated
   per-surface instead of once. Web-verified facts to build on:
   - **NTFS 64-bit file reference = 48-bit MFT record index + 16-bit sequence number.** The old "reserve the
     top byte" idea collides with the sequence number. Reserve a range of MFT-record-*index* values above the
     volume's real MFT high-water mark (validate at attach); derive deterministically
     `index = RESERVED_BASE + H(parentFRN, name) mod SPAN`, fixed non-zero sequence → identical FRN across
     every enumeration info class + `FSCTL_ENUM_USN_DATA` + open-by-id.
   - **Open-by-128-bit-id: high-64-bits==0 ⇒ file id; !=0 ⇒ object id.** Phantom `FILE_ID_128` MUST keep the
     high 64 bits zero, FRN in the low 64.
   - Populate an id→phantom map at enumeration; project ONE canonical descriptor identically into all info
     classes + USN (same wildcard/resume as NTFS). For `SL_OPEN_BY_FILE_ID`, the `STATUS_REPARSE` reissue
     takes Options from the OPEN_PACKET (id-open persists) but the NAME from `FileObject->FileName` — so
     `IoReplaceFileObjectName` with the backing's **binary `FILE_ID_128`**, not a path (that is the `err=87`).
     `phantom.c` already has `SarPmIdMapLookupByRef` + reparse + synthetic-ref machinery — wire the map
     population + the FRN scheme + binary-id reparse.

3. **Constitution rewrite (as-if-original).** III.5 reads "copy-on-first-write"; the realized mechanism is
   **capture-at-write-intent-open** (data-scan section at `IRP_MJ_CREATE` post-op, + pre-create for overwrite
   dispositions once unit 1 lands). Rewrite III.5 and dependents to describe capture-at-open as if it were
   always the design — no changelog, no "was/now". IV.3 stays intact (COO complies). Also `grep` for any
   migration / on-disk-schema-version code and **delete** it (never-shipped project; prior LLM versioning
   traces are mistakes to remove, not preserve).

### 0.3 Verification & environment notes for the successor

- `scripts/build_driver.bat` → `build_driver/semantics_ar.sys`. `scripts/vm_verify_coverage.ps1 -AttackCount N`
  deploys/loads/runs all phases; recovery via `sarctl preserve-recover`.
- VM Hyper-V Gen2 `SarTarget` Win11 26100; PowerShell Direct admin/admin; snapshot `armed-kdnet` (testsigning
  + KDNET) — restore before each run. Live-KDNET: host `kd -k net:port=50000,key=1.2.3.4`; NMI-into-attached-kd
  is the only working hung-guest stack method (LiveKD-hv / vm2dmp / plain NMI-dump all fail on this host — see
  `docs/DEBUGGING.md`).
- **Phase-G "hang" is not a bug:** it is CPU saturation (our Oracle crypto-battery `scan_battery`/`sar_convict`
  on ~1 core + Defender `mpengine` on the rest), diagnosed by live kernel stacks; it completes and passes
  fail-closed if waited out (~5-6 min). Do NOT power-cycle prematurely. Bounding the battery / excluding
  Defender for the test corpus would remove it.
- Constraints (binding): no comments in code; no dead/fallback/compat code; no migration/schema-versioning;
  Constitution edits are as-if-original.

## 1. Final goal (unchanged across the chain)

A single x64 Windows binary set — boot-start minifilter + user-mode service — that realizes
the Constitution: observe every destruction-of-an-existing-original, capture the encryption
key at the write (the Oracle), persist captured keys in a self-protected keystore, recover
files by decrypting their on-disk ciphertext, preserve original content before unrecoverable
destruction (copy-on-first-write, encrypted at rest, bounded window), and respond under
operator-chosen AUDIT/ENFORCE with a user-owned creation-time whitelist. Host-verifiable
logic stays freestanding and tested; kernel-bound and Win32 code is verified in a WDK/VM context.

## 2. Current true state

**[CONSTITUTION PART I — capture→recovery — PROVEN END-TO-END IN A LIVE KERNEL this session (VM-VERIFIED).**
The chain's central claim is now demonstrated on the live VM, not just asserted:
- **Capture conviction works** and was never broken: with a clean keystore a single CNG-AES-256 encryption by
  `test_encryptor.exe` yields `keystore.bin 0 → 802 B` (one record). The earlier `convict=0` reading was a
  *diagnostic-counter placement* artifact (the temp counter sat *after* `SarKeystoreAppend`, which returns 0 on
  a duplicate key_id; `test_encryptor` uses a fixed key, so a pre-existing keystore deduped the append). Moving
  the probe ahead of the dedup showed `convict=1` even when the keystore stayed `802 → 802` — conviction had
  been succeeding all along.
- **Recovery round-trip PASS:** `sarctl list` shows `key_id ↔ \Device\HarddiskVolumeN\sar\<file>`; `sarctl recover
  <key_id> <path>` → kernel decrypt + tag-verify → `.sarrectmp` → service atomic replace → **recovered file
  SHA-256 == golden** (the pre-encryption original), exact byte-for-byte.
- **Verify-before-replace (III.4) PASS:** `recover` against a never-encrypted file decrypts to garbage, the tag
  mismatches → `recover result=-8` (decline), the target is left **byte-for-byte intact**, no `.sarrectmp`
  leftover. The invariant is enforced *in the kernel* (`SarRecoveryExecute`: `sar_recover_verify` gates the
  `SarRecWriteTemp`).
- **Chronic FS deadlock — ROOT CAUSE FIXED and proven gone.** The deadlock that recurred across implementers was
  the capture path doing a *synchronous in-IRP `FltReadFile` of the very file being written* to obtain `P`. The
  fix is **read-correlation**: `SarPostRead` samples the originator's own head read into the per-stream context;
  `SarSubmitWrite` correlates it to the write by offset; the write path performs **zero file I/O**. FS HEALTH OK
  across the prior wedge pattern (in-place overwrite + read), repeatedly.
- **Constitution III.2.1 CORRECTED** (evidence-gated, per the standing rule). The old clause "`P` is the old bytes
  read at the write IRP before release" was the erroneous *HOW* behind the deadlock; it now states `P` is sourced
  from the originator's own read (read-correlation), never read at the write IRP, with the deadlock rationale and
  the confirmed-limit fallback (a write whose plaintext was never observed → no Oracle attempt, VIII.2).
- **Diagnostic instrumentation REMOVED.** The temporary `g_sar_diag_*` counters + `SarDiagWriteRegistry`
  (`HKLM\SOFTWARE\SemanticsArDiag`) used to localize the `convict=0` artifact are fully deleted from
  `driver.{c,h}`, `capture.c`, `commport.c`, `operations.c`; the orphaned registry key is cleaned on the VM.
  Driver rebuilds clean (`cl /kernel`), ctest 9/9, and a post-removal regression on the VM re-proved
  capture (`0 → 802`) + round-trip (recovered == golden) + FS HEALTH OK.
]**

**[RECOVERY-VERIFICATION INVARIANT — DONE and HOST-VERIFIED earlier (ctest 9/9, MSVC `/W4 /WX`
clean).** Constitution III.4 now mandates *verify-before-replace*: recovery checks the decrypted
result against a capture-time **verification tag** before replacing a file; any mismatch declines and
leaves the target byte-for-byte intact. Realized at the engine/format layer, the dependency floor the
kernel round-trip builds on:
- Keystore record format (`keystore_format.h`): each record carries `sample_offset`,
  `sample_length`, and a 32-byte `sample_tag` = SHA-256 of a bounded sample (≤`SEMANTICS_AR_SAMPLE_TAG_MAX`
  = 64 B) of the *original* `P` taken from the Oracle's input at the convicting write. The tag is a check
  value, not a recovery source (III.1.1) and is covered by the record's keyed MAC (tamper test added).
- The tag is computed **inside the freestanding core** (`sar_keystore_record_init` via new `sar_sample_tag`,
  fed by `sar_capture_run` from `req->plaintext`/`sample_size`/`file_offset`) — so the **driver capture path
  needs no change**; it gets the tag for free on its next compile.
- New engine primitive `sar_recover_verify(pt, file_size, &verify)` + `sar_recovery_verify_from_record`
  (`recover.c`): hashes the decrypted sample window at `sample_offset` and constant-time-compares to the
  stored tag → `SAR_RECOVER_OK` or `SAR_RECOVER_DECLINED_MISMATCH`; a zero-length anchor declines (never an
  unverifiable replace).
- Host `sar_recover_file` verification **switched from forward-relation `(P,C)` to tag-based** (the real
  recovery has no `P`; the forward path was a test-only proxy) and the check now gates the *replace* (runs
  after decrypt, before writeback). `test_recover` `no_clobber` rewritten to the tag mechanism: wrong key →
  garbage → tag mismatch → `DECLINED_MISMATCH` + file intact; correct key → match → recovered.
- **Dead-code cleanup forced by the switch:** `sar_recover_confirm` (now uncalled) removed, and
  `forward_relation`'s `strict_iv` parameter + its two `!strict_iv` adjacent-block branches removed (only
  `sar_recover_locate_iv` uses `forward_relation`, always strict). No behavior change to IV-location.
- **Research-grounded (deep-research, two passes, MSRC + WDK primary sources):** kernel-decryption (III.1.2)
  was challenged and **upheld** — MSRC Servicing Criteria: the *Kernel* boundary is serviced, *PPL* is not
  ("Intent to service? No"), so kernel-pool secrecy is strictly stronger than a PPL service against a
  non-admin attacker; the Resiliency-Initiative "move out of kernel" signal is about always-on EDR sensors,
  not bounded on-demand recovery. Verify-before-replace is the universal safety property across PayBreak
  (libmagic), Redemption (transactional commit), Rhea (format validation); we use a cryptographic tag,
  stronger than those heuristics.
- **Key-reuse whole-set recovery — RESOLVED (host-verified, driver compile+link-verified).** The append
  dedup collapsed sibling files under a reused key into one record (one anchor), so mandatory verification
  (III.4) declined every sibling — a real *recovery-correctness* hole, not just enumeration. Fix:
  `sar_keystore_append` now dedups by **(key_id, provenance_path)**, so the keystore holds **one record per
  encrypted file** (Constitution II.5.1's literal model — the old key_id-only dedup was the bug). Recovery
  decrypts with the key, then `SarKeystoreVerifyAnchor` scans the key's records and accepts on the first
  matching tag (no path-canonicalization needed; per-file keying is O(1)). Constitution II.5.1 corrected to
  state the (key, file) record and per-file anchors under reuse.
]**

**[Recovery round-trip — DONE and VM-VERIFIED this session (see the Part I block at the top of §2).** DoD met:
capture a CNG-encrypted file → `sarctl list` shows key_id↔path → `sarctl recover` → file restored to plaintext,
SHA-256 == golden; verify-before-replace declines a tag mismatch with the target intact. Per-item status:
- **(a) `driver/recovery.c` file I/O — DONE via `Zw*`, and the `Flt*` switch is NOT required (evidence).** The
  research deadlock concern was specific to an *in-IRP same-file* read (the capture path, fixed by
  read-correlation). `SarRecoveryExecute` runs as a **standalone PASSIVE operation on the service's request
  thread, not nested in any IRP**, so `ZwCreateFile`/`ZwReadFile`/`ZwWriteFile` (OBJ_KERNEL_HANDLE,
  `FILE_SYNCHRONOUS_IO_NONALERT`) complete cleanly — round-trip PASS + FS HEALTH OK prove it. `sar_recover_verify`
  gates `SarRecWriteTemp` (III.4). *Optional hardening, not correctness:* `Flt*`-with-own-instance would avoid our
  own filter re-capturing the recovery read/write (currently benign — the temp write bails on no correlated `P`).
- **(b) `driver/commport.c` buffer hardening — the live paths run fault-free** (handshake + `RECOVERY_EXEC`
  exercised live, `SarCommMessageNotify` SEH-wrapped). Full `IS_ALIGNED` audit of every `OutputBuffer` write
  per the MiniSpy pattern is still worth a dedicated pass but no fault has surfaced.
- **(c) Catalog + operator tool — DONE, keystore-backed (host + driver compile-verified).** The
  VERDICT_NOTIFY shadow (`service/control.c g_catalog`) is **removed**; `SAR_CTL_OP_LIST` now drives the new
  authenticated `CATALOG_QUERY`/`CATALOG_REPLY` (protocol types 11/12, handshake-gated). The driver enumerates
  the keystore under the push lock **shared** (`SarKeystoreProject`, one record/index → one entry) and projects
  only the non-secret tuple {key_id, algorithm, mode, provenance_offset, provenance_path} — never key_bytes/iv/
  sample_tag. Single-entry-per-reply keeps the reply (~596 B) inside the existing 1024-B transport with no
  buffer/stack change; the service pages `index=0,1,…` until `valid==0`. **The catalog now survives service
  restart and reboot** (it reads the persisted keystore), closing the restart-empties-the-list defect. The
  projection is the same tuple VERDICT_NOTIFY already pushed, so no new boundary crossing (Constitution III.1.3
  added to govern enumeration). `tools/sarctl` unchanged.
- **(d) Per-file recovery anchor under key reuse — DONE** (see the key-reuse RESOLVED note above; the catalog is
  now one entry per captured file, so a reused key's whole recoverable set is enumerable and recoverable).
*Verification:* the user's admin-PowerShell→relay VM loop.]**

**Done and VM-verified — FIRST LIGHT (live kernel, Win11 24H2 Hyper-V):** the driver — for the
first time in the project — **loads and runs in a live kernel.** Built reproducibly by
`scripts/build_driver.bat` (`cl /kernel` against WDK 26100, `semantics_ar.sys` ~127 KB), embedded-
test-signed + cataloged by `scripts/package_driver.ps1`, installed via `pnputil /add-driver …
/install` (after trusting the test cert with `certutil -addstore Root/TrustedPublisher`), and
loaded with `fltmc load semantics_ar`: **exit 0, no bugcheck.** `fltmc instances` confirms
**universal attach** at altitude 385000 to `C:`, `D:`, `\Device\Mup`, etc. — so `DriverEntry`,
`FltRegisterFilter`/`FltStartFiltering`, and `InstanceSetup` all execute correctly at runtime. The
VM harness is driven host-side over PowerShell Direct (the guest console is not observed); crash
detection is automatic (Secure Boot off + `testsigning on` made permanent by removing the Hyper-V
automatic checkpoint; service forced to demand-start to avoid a boot-loop on a buggy load; bugcheck
inferred from a `LastBootUpTime` change + the System-log 1001 event + `MEMORY.DMP`).
**The signed-challenge handshake now COMPLETES end-to-end** (service reaches `SERVICE_RUNNING`,
exit 0): the guest service signs the driver's nonce (`NCryptSignHash`, machine KSP key
`SemanticsArServiceKey`) and the driver verifies it (`BCryptVerifySignature`, embedded matched
public blob) → version check → `STATUS_REPLY` → run loop. So the full driver↔service trusted
control plane runs in a live kernel. **Three real runtime bugs were found and fixed to get here**
(all only observable by running, none caught by compile/link): (1) the driver resolved kernel BCrypt
via `MmGetSystemRoutineAddress`, which only resolves ntoskrnl/hal exports → always NULL → connect
refused; fixed to direct BCrypt calls + `cng.lib` (same bug in `keystore_persist.c`). (2) the
handshake challenge was sent via `FltSendMessage` from inside `ConnectNotify` before the connection
is established; deferred to a post-connect `FltQueueGenericWorkItem`. (3) the service validated
received messages against the full receive-buffer capacity, not the header's `message_length`, so
every driver-pushed message tripped `sar_msg_validate`'s exact-length (OVERSIZED) check; fixed to
validate against the declared length bounded by capacity. The service opens the key from the
**machine** store (`NCRYPT_MACHINE_KEY_FLAG`, was missing). *Provisioning/deploy note:*
`service_pubkey.h` is regenerated per deployment to match the target's machine key; the driver's
ImagePath was pointed at a writable dir on the test VM (`C:\sar`) so iterative redeploys don't fight
the TrustedInstaller ACL on `system32\drivers`. **Still unrun: the capture path (the heart) and
recovery** — the next and central frontier, gated on the C1 heap-scan correction (see §2 CORRECTION
and §4 item 0).

**[PRESERVATION (Constitution Part III.5) — IMPLEMENTED full-stack, VM-VERIFIED (2026-06-27, 24/24 PASS).]**
Copy-on-first-write before destruction, the circumstantial bounded-recovery asset complementing
the Oracle. Implemented across all layers and **proven end-to-end on live Win11 24H2 (SarTarget VM)**:
- **Engine freestanding core** (`engine/include/sar_preserve.h`, `engine/src/preserve.c`): covered
  (first-write-wins), append, reconcile (containment — key region must contain preserved region),
  age/capacity eviction, verify-restore (path+offset+length binding + content_tag SHA-256),
  serialize/verify (chained MAC + anchor rollback detection).
- **On-disk format** (`common/include/semantics_ar/preserve_format.h`): `sar_preserve_record_t`
  (provenance, offset, length, capture_time, IV, content_tag), `sar_preserve_header_t` (magic
  `0x50524153`, generation, head_mac), `sar_preserve_disk_record_t` (per-record MAC). Packed.
- **Driver kernel glue** (`driver/preserve.c`, `driver/store_io.c`): BCrypt AES-256-CBC
  encrypt/decrypt with per-op key object + IV copy + secure zeroize; free-extent-first data file
  allocator; debounced persist thread (5 s); push lock; paged pool records (cap 65536); key
  generation via `BCryptGenRandom`; `SarStoreWriteAtomic` (write→flush→rename); SYSTEM+Admins-only
  directory DACL. Encryption buffers use paged pool (ctbuf, padbuf in SarPreserveStage/Restore).
- **Integration — capture** (`driver/capture.c`): `SarCaptureSubmitWrite` reads pre-modification
  data via `FltReadFile` from PreWrite (cached read from the initiating instance — does NOT trigger
  the instance's own callbacks; safe because PreWrite fires before NTFS acquires any lock).
  `SarCapturePreserveFromWork` validates FltReadFile data against the PostRead pre-image sample,
  stages via `SarPreserveStage`. `SarCaptureCommit` calls `SarPreserveReconcile` after keystore
  append (III.5.4). ENFORCE capacity exhaustion: early blocking in `SarCaptureWorker` right after
  `sar_gate_classify` (before the expensive process memory scan) + `SarCapturePreserveFromWork`
  checks `SarPreserveWouldExceed` before the `preserve_buf` null check (so FltReadFile failure
  cannot bypass the capacity gate).
- **Integration — lifecycle** (`driver/driver.c`): `SarPreserveCreate` in DriverEntry (non-fatal,
  sets `preserve_active`); `SarPreserveLoad` in boot-reinit + post-boot; `SarPreserveDestroy` in
  unload (ordered: capture→preserve→keystore→comm→filter→state).
- **Integration — recovery** (`driver/recovery.c`): `SarPreserveRecoveryExecute` resolves DOS→NT
  device path via `SarRecResolveNtPath` (`IoCreateFileSpecifyDeviceObjectHint` +
  `ObQueryNameString`), calls `SarPreserveRestore` (decrypt + content_tag verify), splices restored
  bytes, writes temp. Path resolution is required because the preserve index stores NT device paths
  (from `FltGetFileNameInformation` in kernel) while user-space tools pass DOS paths.
- **Integration — self-protection** (`driver/operations.c`): preserve store files under
  `\SystemRoot\System32\drivers\SemanticsAr\` protected by `SarNameIsOwnStore` + anti-deletion.
- **Protocol** (`protocol.h`): messages 13–17 (PRESERVE_QUERY/REPLY, PRESERVE_RECOVER,
  PRESERVE_RESULT, SET_BUDGET); `control/src/msg.c` size validation.
- **Service** (`service/control.c`, `service/recovery.c`): PRESERVE_LIST pagination via
  `sar_preserve_fetch`, PRESERVE_RECOVER → `sar_preserve_recovery_run` → atomic replace,
  SET_BUDGET forwarding.
- **CLI** (`tools/sarctl.c`): `preserve-list`, `preserve-recover <path> <off> <len>`, `budget
  <retention-sec> <capacity-MB>`.
- **Tests** (`tests/test_preserve.c`): 13 assertions covering first-write-wins, distinct
  region/file, reconcile containment, age/capacity eviction, would_exceed, verify-before-restore
  (content_tag + binding), serialize/verify/anchor (round-trip, tamper, rollback).
  `tests/harness/preserve_test.c`: VM test tool (per-file random AES-256-CBC in-place encryption).
- **Build**: `scripts/build_driver.bat` includes `store_io` + `preserve` TUs; all three
  `CMakeLists.txt` (driver, engine, tests) include preserve sources.
- **Constants** (`driver/driver.h`): retention default 7 days, capacity default 10 GB, record cap
  65536, stage max 4 MB, sector 4096.

*VM verification script:* `scripts/vm_verify_preserve.ps1` — 10 phases, 18 assertions:
- Phase 1: Package & deploy (driver load + service start)
- Phase 2: Preservation staging (5 files 16B–64KB, all staged, all encrypted)
- Phase 3: First-write-wins (re-encryption does not duplicate entries)
- Phase 4: Restore round-trip (5/5 files restored to golden SHA-256)
- Phase 5: Oracle→Preservation reconciliation (Oracle conviction removes preserve entry)
- Phase 6: Large files (256KB, 512KB, 1MB — all staged + restored to golden)
- Phase 7: Self-protection (all 5 store files resist user-mode deletion)
- Phase 8: Budget control (sarctl budget set + restore)
- Phase 9: ENFORCE capacity exhaustion (1MB budget, 20×128KB files: 0 modified, 20 blocked)
- Phase 10: Edge cases (sub-SAR_CANDIDATE_SIZE skip, double-encrypt identity)

*Bugs found and fixed during VM verification (all structural, not band-aids):*
- **NT path vs DOS path mismatch** (`driver/recovery.c`): preserve index stores NT device paths
  (`\Device\HarddiskVolume3\...`) from kernel-side `FltGetFileNameInformation`; user-space passes
  DOS paths (`C:\...`). Added `SarRecResolveNtPath` to resolve the namespace boundary.
- **ENFORCE would-exceed check ordering** (`driver/capture.c`): `SarCapturePreserveFromWork`
  checked `preserve_buf == NULL` before `SarPreserveWouldExceed` — if FltReadFile failed, the
  function returned without checking capacity, so blocking never triggered. Moved capacity check
  before the null check.
- **Late ENFORCE blocking** (`driver/capture.c`): blocking decision was after the expensive
  `SarCaptureSnapshotProcess` (~500ms on VM). By the time the worker blocked the process,
  subsequent writes had already gone through. Added early blocking right after `sar_gate_classify`
  (cost ~0), before the process scan. VM result: 20/20 writes blocked with 1MB budget.

*Code-level audit finding:* the offset-0 pre-image filter in `SarPostRead` is a deliberate
correlation-model heuristic, not a bug — the Gate D∧T requires old-vs-new comparison at the same
offset, and removing the filter would break the dominant ransomware pattern's correlation (later
chunk reads overwrite the offset-0 sample). Non-zero-offset writes that lack a correlated read
sample are a confirmed limit of the correlation model (VIII.2 scope).

*Persist/reboot verified (Phase 11):* 10 preserve entries survived VM reboot intact, post-reboot
restore returned the exact golden SHA-256, and new staging worked after reboot. The full
preservation lifecycle — stage → persist → reboot → load → restore → new stage — is proven.

**Done and host-verified (builds clean `-Werror`/`-Wconversion` on Linux gcc and `/W4 /WX` on
MSVC; `ctest` green):**
- `engine/` — ciphers, conviction battery, keystore v2 (MAC chain + anchor compare), recovery
  transform, Gate-T. (Slices 1–3.)
- `control/` — the freestanding chassis control-plane core: message-header validation, whitelist
  match + identity-state resolution, AUDIT/ENFORCE mode SM, connect-handshake/challenge SM.
  `tests/test_chassis.c`, 64 assertions incl. a 200k-iteration parser fuzz. (Slice 4.)
- `capture/` — the freestanding capture core (Unit 2): Gate-T → `sar_convict` orchestration and
  the verdict→keystore-record→`VERDICT_NOTIFY` projection with a tested zero-plaintext-key-leak
  invariant. `tests/test_capture.c`, 51 checks (ECB/CBC conviction, scan-buffer-primary path,
  gate skip, no-conviction, invalid inputs, provenance, key_id determinism, no-leak). See
  `SLICE5_DESIGN.md`.
- `engine/sar_keystore_mgr.{h,c}` — the keystore load/persist policy core (Unit 3-Core): the
  anchor/generation state machine (crash-window accept vs. rollback/erasure/tamper reject) over
  the existing serialize/verify. `tests/test_keystore_mgr.c`, 15 checks (round-trip restore,
  crash window, rollback, same-gen tamper, erasure, corrupt, wrong-key, overflow). See
  `SLICE6_DESIGN.md`.
- Contract: `protocol.h` signed-challenge handshake + `STATUS_REPLY`; validator/tests cover
  message types 1–10. The `VERDICT_NOTIFY` projection is now exercised by `test_capture`.

**Compiles + links against the real WDK, but NEVER LOADED/RUN (no runtime/VM validation yet):**
A WDK (26100 km headers + libs) is present on this host. The **entire `driver/` tree** — chassis
(Slice 4) + Unit 2 capture + the freestanding `engine` battery/`capture` core compiled into the
`.sys` (Slice 1: battery in kernel) — compiles clean with `cl /kernel` at `NTDDI_WIN11_GE` and
links into `semantics_ar.sys` (≈95 KB). The build-out fixed several real bugs (lookaside API,
`FLT_REGISTRATION`, missing `ntsystem.h` declarations, NTDDI, `_rotl` intrinsic — see §6). What is
proven is **compile + link**, not behavior: it has not been loaded, attached, or exercised.
- `driver/` chassis (Slice 4): registration table, seams, comm port + handshake, non-paged state,
  process-create identity, feature/posture.
- `driver/capture.{c,h}` (Unit 2, this session): two-phase kernel capture — **Phase 1** (pre-op,
  requestor context) copies `C`, reads pre-image `P` (`FltReadFile`), resolves provenance, snapshots
  the originator thread's own user stack (`PsGetCurrentThreadTeb` + `NT_TIB`, SEH-bounded 16 KiB),
  queues a generic work item, releases the IRP immediately; **Phase 2** (deferred `PASSIVE_LEVEL`
  worker) runs the freestanding core (gate → convict → project) → keystore append + `VERDICT_NOTIFY`
  send + ENFORCE originator registry. `EX_RUNDOWN_REF` drain + in-flight backpressure; lookaside
  work items. `operations.c` routes captures here and applies the ENFORCE block; `driver.c` owns the
  capture lifecycle. Memory-scan is primary; register-grab is a named follow-on (`SLICE5_DESIGN.md`).
- `driver/ntsystem.h` (this session): driver-declared semi-documented system structs/enums/prototypes
  (`PS_PROTECTION`, `SYSTEM_CODEINTEGRITY_INFORMATION`, `SYSTEM_ISOLATED_USER_MODE_INFORMATION`,
  `Zw*`) absent from the WDK km headers — **layouts need VM runtime validation** (§6).
**Done and host/compile-verified this session (Unit 6 — service build + recovery backend + driver↔service
contract reconciliation):**
- `service/` (user-mode exe) now **COMPILES + LINKS** clean (`/W4 /WX`, VS2022 x64, `semantics_ar_service.exe`
  ~73 KB) against the real Win32/CNG/WinTrust/fltlib SDK — the entire user-mode half had never been through a
  compiler before. Built via CMake (`build_win`), which configures `engine`+`control`+`capture`+`service`+`tests`
  on Windows (the `driver/` subdir self-skips when the WDK is not found by CMake; the driver is built by the
  standalone `cl /kernel` recipe in §6).
- `engine/host/recover_file.c` gained a **Win32 atomic-write backend** (the HANDOFF §4.2 engine prerequisite);
  `semantics_ar_recovery_host` now configures and builds on Windows. `tests/test_recover` is now enabled on
  Windows and **passes** (`ctest` 7/7 green on Windows incl. the no-clobber writeback over real files) — this is
  the host-verified, end-to-end exercise of the Win32 `ReplaceFileW` recovery path.
- The driver was **re-verified** (`cl /kernel` `COMPILE_OK`/`LINK_OK`) after the signing-scheme reconciliation
  below.
- The recovery **resolver** (`sar_recovery_resolver_fn`) remains a **declining stub** by design — wiring it to
  the kernel keystore-read is Unit 4 (below). This is the only intentional open seam in `service/`; it never
  fabricates keys.

**Compiles + links against the real WDK (this session):** `driver/keystore_persist.{c,h}` (Unit 3-Core
kernel glue) and the edits to `driver/capture.c` / `driver/driver.c` / `driver/driver.h`, plus
`engine/src/keystore_mgr.c` compiled into the `.sys`. The `SAR_KEYSTORE` object owns the non-paged
record array, push lock, MAC key, generation, and anchor; boot-time load runs from an
`IoRegisterBootDriverReinitialization` callback; a dedicated system thread does debounced atomic
persists (tmp + `ZwFlushBuffersFile` + rename); captures are gated on `SarKeystoreReady` until the
load completes and self-writes to the keystore directory are skipped. Every `driver/`+`engine` TU
compiles clean (`cl /kernel` `NTDDI_WIN11_GE`, `/W4`, only Microsoft-header noise) and all link into
`semantics_ar.sys` (~102 KB, `link` exit 0). The build-out fixed one real omission —
`keystore_mgr.c` was missing from `driver/CMakeLists.txt` (would have left `sar_ksm_*` unresolved) —
and reconfirmed the §6 `capture.obj` name-collision caveat (the core gets a distinct `/Fo`). Behavior
is **unrun**: VM validation remains. See `SLICE6_DESIGN.md`.

**Done and host-verified this session (Realization V0 — handshake crypto realized + INF
correctness; the bring-up prerequisites that block ever loading the product):**
- **Handshake key pair is real, no longer a placeholder.** `tools/provision_handshake_key.c`
  creates the persisted ECDSA-P256 key `SemanticsArServiceKey` (machine store + SYSTEM/Admin DACL;
  `--user` for a no-elevation dev key), exports the `BCRYPT_ECCPUBLIC_BLOB`, and writes
  `driver/service_pubkey.h`. `driver/commport.c` now `#include`s that header instead of the
  all-zero placeholder array it carried (which could never have authenticated anyone). The public
  key and the private key are produced together at provision time and cannot be a committed constant
  (committing the private half would be a service-impersonation hole); the committed
  `service_pubkey.h` is a dev default to be regenerated at deployment.
- **The exact production handshake scheme is host-proven** by `tests/test_handshake_crypto.c`
  (17 checks, `ctest` green): user-mode `NCryptSignHash` over a **raw 32-byte nonce, NULL padding**
  ↔ kernel-shape `BCryptVerifySignature` of the same nonce with the imported 72-byte public blob;
  asserts blob magic `0x31534345`/`cbKey==32`, 64-byte IEEE-P1363 `r‖s` signature (not DER),
  tamper rejection (nonce + sig), and no cross-key acceptance. This closes the prior §3 "verify by
  build, not doc" items (signature size, magic literal, raw-nonce-as-input).
- **INF correctness — `InfVerif /h` clean (exit 0) under WDK 26100.** `driver/semantics_ar.inf`:
  removed the four `DefaultUninstall*` sections (prohibited for primitive drivers since Win10 1903),
  set a non-empty `DriverVer`, and moved `Instances/DefaultInstance/Altitude/Flags` under the
  `Parameters` subkey — the last is **required by the WDK-26100 InfVerif gate** (it errors 1323 on
  bare `Instances`; a dual-write INF carrying *both* bare and `Parameters` keys was tested and **also
  rejected** — InfVerif forbids the bare keys' presence at all). **OPEN MATRIX ITEM (does not narrow
  the IX support goal; it is a packaging question):** whether a `Parameters`-only INF *attaches at
  runtime on pre-24H2* (Win10 22H2 / Server 2019-2022) is **undocumented** — MS Learn is silent and
  the matching sample-repo issue (#553) closed unanswered, so it is a VM-matrix empirical, not a
  guess. The `.sys` binary is single/unaffected (IX.1 intact); only registry packaging is at issue.
  Resolution paths, to decide with VM evidence: (i) if pre-24H2 FltMgr also reads
  `Parameters\Instances` → the one Parameters-only INF serves all; (ii) else use **OS-version-decorated
  DDInstall/AddReg sections** (one INF: down-level section writes bare `Instances`, 24H2 section writes
  `Parameters` — the documented single-INF-multi-OS mechanism, IX.1.1 superset), or build the
  down-level package with a down-level InfVerif. **First Light should run on Win11 24H2** (the committed
  INF matches it and is InfVerif-clean); the down-level matrix + this INF resolution is its own unit.

**[CAPTURE — VM-VERIFIED end-to-end this session (live Win11 24H2, demand-loaded); design corrected by the
live kernel.** A controlled CNG encryptor (`BCryptGenerateSymmetricKey` AES-256-CBC, in-place 48 KB
overwrite, key held live ~15 s then destroyed) is captured end-to-end: driver loads + attaches (no
bugcheck), a deferred worker attaches to the still-live writer and snapshots its memory, the structural AES
detector finds the schedule, the forward `(P,C)` proof convicts, and `keystore.bin` grows. Proven live: the
structural detector, the `KeStackAttachProcess`+`ZwQueryVirtualMemory(handle)`+`MmIsAddressValid` resident
copy, and the keystore append/persist.

**TWO design corrections the live kernel FORCED (both now in code):**
- **Memory capture must run OFF the IRP.** The first design snapshotted SYNCHRONOUSLY in the write pre-op;
  it intermittently HARD-HUNG the kernel (FS/memory-manager re-entrancy from `ZwQueryVirtualMemory` inside
  the write IRP — no dump, force-reboot). Reverted to a **deferred worker** that attaches to the still-live
  writer under `PsAcquireProcessExitSynchronization`+`KeStackAttachProcess` and enumerates via an explicit
  process handle — off the IRP, holding no FS resource, so it cannot deadlock. This vindicates the
  Constitution's original II.3.2 ("memory scan only after the IRP is released"); the zeroization defense
  weakens to best-effort (prompt worker; tight key-destroy/exit = VIII.2).
- **The flat byte-budget snapshot is WRONG (coverage vs cost, proven by live A/B).** Snapshotting the first
  N bytes of committed-private from address 0: 1 MiB MISSES the heap (ASLR → key high → no capture); 4 MiB
  (≈ whole small process) COVERS it but a no-key MISS then scans the whole region — catastrophic on the slow
  VM (hard-hung before; ~15-30 s after a brute-force 64 KiB cap + `candidate_viable` removal). **The real fix
  is heap-targeting** (snapshot only the PEB `ProcessHeaps` regions — small, contains the key); §4 item 0(b),
  now precisely scoped. `SAR_CAPTURE_HEAP_BUDGET` is 4 MiB (captures, NOT production-ready) pending it.

Bring-up fix also required: the keystore-ready gate never opened on a **demand-loaded** driver
(`IoRegisterBootDriverReinitialization` fires only for boot-start) → read the service `Start` in
`DriverEntry`, call `SarKeystoreLoad` directly when post-boot. **Test caveat: the VM is very
slow/underpowered with NO kernel debugger, so hard-hangs leave no dump and live iteration is costly — set up
KDNET before deeper capture debugging.** Driver is DbgPrint-silent; VM observation is `keystore.bin`.]**

**[CAPTURE REGION + ZEROIZATION DEFENSE — IMPLEMENTED + VM-VERIFIED this session]**
The Phase-1 stack snapshot was the wrong region for the dominant case: MS CNG keeps the AES key
schedule in a **heap-allocated key object reused across every write** (`BCryptGenerateSymmetricKey`
`pbKeyObject`), and the writer can `BCryptDestroyKey`-zeroize it (FIPS-confirmed prompt zeroization)
the instant the producing write returns. Resolution (now in code, after the live kernel rejected a
synchronous-in-IRP snapshot — see the VM-verified note above): a **deferred worker** attaches to the
still-live writer (`PsAcquireProcessExitSynchronization` + `KeStackAttachProcess`) and snapshots its
committed-private regions via an explicit process handle (`driver/capture.c` `SarCaptureSnapshotHeap`:
`ObOpenObjectByPointer` → `ZwQueryVirtualMemory(handle)`, `MmIsAddressValid` resident filter, SEH copy into
a `SAR_CAPTURE_HEAP_BUDGET` paged buffer); the analysis runs on the frozen bytes. Off the IRP, so no FS
re-entrancy. The flat byte-budget is a known coverage/cost flaw → heap-targeting is the fix (§4 item 0(b)).
To make the structural scan tractable the engine
gained a **cheap AES key-schedule structural detector** (`aes_schedule_is_valid` + `aes_schedule_scan`,
aeskeyfind recurrence over 176/208/240 B at any byte offset, single round-key→master inversion, then
the existing forward `(P,C)` proof convicts) — host-verified by `tests/test_schedule.c` (11 checks,
including capture of a schedule planted at a 16-unaligned offset that the old 16-stepped scan misses).
This **did require a Constitution amendment** (II.3 snapshot-vs-scan + key-live timing; II.4.1
structural location; II.4.2 per-key cumulative-N; VIII.2 structureless-key budget + key-destruction
cases) and an **engine change** (the detector) — the earlier "not a Constitution/engine change" framing
was wrong: ChaCha/raw-master keys have no structure (located only by bounded `(P,C)` trial, VIII.2),
and full-heap AES coverage needs the cheap detector. VM validation remains (§4 item 0, §6).

**Forbidden-concept grep** (`preserve|shadow|journal|evict|saturat|capacity-limit|trusted_pid|
self_trust|auto_block|access_denied`) is empty over the host-verifiable + freestanding tree and
every filesystem-callback path **except two Constitution-mandated `access_denied` occurrences**:
the comm-port `ConnectNotify` reject in `driver/commport.c` (VII.3; `SLICE4_DESIGN.md` JUDGMENT
CALL) and the V.1 ENFORCE block in `driver/operations.c` `SarPreWrite` (`SLICE5_DESIGN.md`). Both
are downstream-of-verdict / client-reject acts, not the old block-the-write residue.

## 3. Decisions & rationale not derivable from code

Recorded in full in `SLICE4_DESIGN.md`: **D1** ENFORCE anchoring (seam holds a referenced
originator `PEPROCESS`/`PETHREAD` taken in the requestor thread context at pre-op, so a later
block lands on the right live-context op, never the decoupled paging flush); **D2** wiper-member
routing (keyless destruction → telemetry seam, never the capture seam); the **two-layer auth
split** (Layer A canonical-identity + signed challenge finishes the chassis without the
MVI/ELAM gate; Layer B PPL query is recorded-and-degraded); the **`STATUS_ACCESS_DENIED`
judgment call** vs the forbidden grep; and the **OS floor** (self-protection-intact Win10 22H2 /
Server 2022; recovery-only Win10 1809 / Server 2019).

Recorded in `SLICE5_DESIGN.md` (Unit 2 capture path): the **freestanding/kernel split** (orchestration
+ projection are host-tested; only the grab/scan/deferral are kernel); **memory-scan is primary,
register-grab deferred** (XMM is clobbered by the write IRP, the key schedule lives in memory); the
**Phase-1 stack snapshot** (vs a Phase-2 cross-thread TEB read, which is undeclared and wrong-context);
the **second mandated `access_denied`** (V.1 ENFORCE block); and the **rejected** options (entropy
pre-screen — IV.2.3; in-kernel process termination — scope/philosophy). The driver build-out's
cross-cutting fixes (FLT_REGISTRATION, `ntsystem.h`, NTDDI, `_rotl`) are in §6.

**Unit 6 decisions (external-validated against primary Microsoft sources; not derivable from code):**
- **Handshake = ECDSA-P256 over the raw 32-byte nonce, no hash, no padding.** The build-out found the
  handshake could *never* have authenticated as written: the driver verified RSA-PKCS1 over the *raw* nonce
  while the service signed PKCS-less (`NCryptSignHash`, `pPaddingInfo=NULL`) over `SHA-256(nonce)` — both a
  scheme mismatch *and* an input mismatch (the driver never hashed). Resolved to ECDSA-P256 because the service
  was already padding-less (the documented ECDSA shape) and ECDSA removes the padding agreement point entirely
  and shrinks the compiled-in public key. The 32-byte nonce is used directly as the signed value on both sides
  (a high-entropy single-use challenge needs no pre-hash). Driver verify side switched to
  `BCRYPT_ECDSA_P256_ALGORITHM` + `BCRYPT_ECCPUBLIC_BLOB` + NULL padding; the compiled-in key is an ECC-P256
  public-blob **placeholder** (provisioned at deployment, like the prior RSA placeholder).
- **Win32 recovery writeback** = write temp in the same dir → `FlushFileBuffers` → **`ReplaceFileW`** (NULL
  backup honours no-preservation; preserves the original's DACL/timestamps/object-id/ADS) with a
  **`SetFileInformationByHandle(FileRenameInfoEx, REPLACE_IF_EXISTS|POSIX_SEMANTICS)` fallback** when the target
  is held open (e.g. by the still-running ransomware) — POSIX-semantics rename wins that race. The POSIX
  `fsync(parent-dir)` step was **dropped on Windows** (no equivalent; a directory `FlushFileBuffers` is
  undefined and fails on SMB — rename durability rides NTFS journaling). `REPLACEFILE_WRITE_THROUGH` is
  documented "not supported," so it is not used.
- **Identity verification hardening** (`service/identity.c`): hash + Authenticode trust are now bound to a
  **single locked handle** (`FILE_SHARE_READ` only → denies write/delete/rename for the eval window) to close
  the TOCTOU between hashing/verifying and the path-based signer lookup; revocation is **cache-only**
  (`WTD_CACHE_ONLY_URL_RETRIEVAL` + `WTD_REVOKE_WHOLECHAIN`) to avoid a network stall on the create hot path;
  the signer name is read via the explicit `CERT_NAME_ATTR_TYPE`+`szOID_COMMON_NAME` (deterministic, vs the
  `SIMPLE_DISPLAY` fallback chain). **Content hash stays the plain file SHA-256** (not the Authenticode PE
  hash): for a narrow, exact-match, revocable exemption a tighter "any byte change re-verifies" binding is
  preferable; re-sign stability is an anti-feature here. *Recorded hardening not taken (content-hash already
  neutralizes it, so it does not escape Constitution 0.3): pinning issuer + leaf SHA-256 thumbprint instead of
  the subject string — a future option if the identity tuple is ever widened.*
- **NCryptSignHash deadlock avoidance**: the service reports `SERVICE_RUNNING` **before** opening the signing
  key / connecting / running the handshake, because `NCryptSignHash` must not be called from the
  StartService/ServiceMain path while the SCM start-lock is held (primary-source Remark). The crypto now runs
  after the lock is released.
- **Operator control pipe DACL** (`service/control.c`) tightened from a **NULL DACL (every local process)** to
  SYSTEM + Administrators only (`D:P(A;;GA;;;SY)(A;;GA;;;BA)`) — the pipe drives mode/whitelist, so an open DACL
  was an authority hole.
- **Port name single-sourced** to `protocol.h` (`SAR_COMM_PORT_NAME`), removing the driver/service duplicates.
- Service receive path no longer mis-reads `FILTER_MESSAGE_HEADER.ReplyLength` as the inbound payload length
  (`ReplyLength` is the *expected reply* size, zero for fire-and-forget) — inbound length now comes from the
  app header via `sar_msg_validate`.

**Unit 4 decisions (external-validated against primary Microsoft sources + a focused re-verification
pass; not derivable from code):**
- **"Design C" — recovery decrypts in the kernel; the optimal split.** Cost was declared no object; the
  theoretically-best secrecy posture keeps both the captured key *and* the recovered plaintext inside the
  kernel. The kernel reads the ciphertext, decrypts with its in-pool key, and writes the plaintext temp;
  the user-mode service handles **paths only** and does the `ReplaceFileW` swap (whose free DACL/ADS/
  object-id/creation-time preservation is the reason file I/O stays in user mode). Rejected **Design A**
  (decrypt in the PPL service, key shipped over the port) and full **Design B** (kernel also does the
  atomic replace + attribute copy). Design C dominates both: it needs no kernel re-implementation of
  `ReplaceFileW`'s attribute preservation, and it **preserves** the Slice-1 "no plaintext keys cross the
  port" contract instead of revising it — the port carries only key_id + target path + status.
- **PPL is not a secrecy boundary against admin (confirmed), but this does NOT force Design B over A — and
  Design C moots it.** MSRC's *Windows Security Servicing Criteria* (read directly): PPL is defense-in-depth,
  "Intent to service? **No**," and its stated goal guards only against **non-administrative** non-PPL
  processes; admin is part of the TCB (admin→kernel is explicitly **not** a security boundary). So the only
  threat that distinguished A from B (admin reads the PPL service's memory) is one Microsoft never scoped
  PPL to defend *and* one Constitution 0.3/VII.4/VII.5 already treat as out-of-scope/recorded-descent.
  The deep-research framing of "Design A premise collapsed → switch to B" was **overstated**; Design C
  sidesteps the question by never exporting the key at all.
- **POSIX-semantics rename DOES win the held-open race (deep-research's 0-3 refutation was a verification
  artifact).** Read directly from MS-FSCC `FileRenameInformationEx`: `FILE_RENAME_POSIX_SEMANTICS` +
  `REPLACE_IF_EXISTS` — "Existing handles to the replaced file continue to be valid. Any subsequent opens
  of the target name will open the renamed file, not the replaced file." So `sar_atomic_replace_file`'s
  fallback correctly replaces a target a still-running ransomware holds open (NTFS; ReFS/SMB are VM items).
- **ReplaceFile has no write-through flag** (`REPLACEFILE_WRITE_THROUGH` is documented "not supported");
  durability rides `FlushFileBuffers` (kernel `ZwFlushBuffersFile` on the temp) + NTFS journaling for the
  rename. ReplaceFile preserves creation time but **not** last-write/last-access — acceptable for recovery.
- **METHOD_NEITHER TOCTOU:** the recovery handler snapshots the request (`RtlCopyMemory` into a kernel
  local) before validate-and-use; the request is handshake-authenticated like `SET_MODE`.

## 4. Remaining work (ordered; each: boundary / definition-of-done / deps)

> **Unit 2 (Capture path) is DONE this session** — freestanding core host-verified
> (`capture/`, `test_capture`), kernel glue compile+link-verified but unrun (`driver/capture.c`). DoD met for
> the host-replayable substance (convicted replays yield a keystore append + `VERDICT_NOTIFY`;
> the no-key-leak invariant is tested; the gate runs in the deferred phase, never on the IRP wait
> path; ENFORCE blocks the originator's next live-context write via the D1 referenced
> `PEPROCESS`). Memory-scan is primary; the XMM register-grab lane, mmap/section capture,
> clone/offload `(P,C)`, and a heap-walk scan are **named follow-ons** in `SLICE5_DESIGN.md`. The
> kernel glue now **compiles + links against the real WDK** (§6); only runtime/VM behavioral
> validation remains.

0. **Realization track (the gate to everything VM-bound; this is where "compiles" becomes "runs").**
   *V0 (provisioning/handshake/INF) is host-done this session (§2).* **Remaining V0:** a
   test-signing + packaging step (Stampinf → Inf2Cat → signtool on the proven `cl /kernel` build, or
   migrate the driver to a WDK vcxproj/EWDK) producing a signed `.sys`+`.cat`. **V1 (First Light):**
   a Hyper-V Gen2 VM (Secure Boot off, `bcdedit testsigning on`, KDNET) that loads the driver,
   attaches to a volume (`fltmc`), and completes the now-real signed-challenge handshake with the
   service. *DoD:* `fltmc filters` shows the filter; the service authenticates over the port (KD
   evidence). *Deps:* the user provides the VM + (ideally) PowerShell Direct; see §5 prep notes.
   **Capture-region + zeroization defense (the heart): IMPLEMENTED (compile+link + host-verified
   engine), VM validation remaining.** The synchronous resident-only heap snapshot and the engine
   structural detector are in code (§2). The remaining work is purely VM-empirical, and these are the
   measurements literature cannot supply (settle them in the VM, do not re-research):
   (a) **CNG plaintext-schedule residency — RESOLVED (measured this session, user-mode, no VM).** A probe
   (`BCryptGenerateSymmetricKey`+`BCryptEncrypt`, AES-256, ECB) showed `BCRYPT_OBJECT_LENGTH=654` and the
   **full 240-byte forward AES-256 expanded schedule resident at offset 128** of the key object (validated
   by `aes_schedule_is_valid` at threshold 0; standard FIPS forward layout, not byteswapped, not
   Key-Locker-hidden), with the bare master also at offset 92. So `aes_schedule_scan` is live for the
   dominant CNG case — the single fact the structural detector rides on is confirmed. (AES-128/192 keep
   the identical key-object mechanism; only the 256 case was measured.)
   (b) **snapshot region selection — heap-targeting IMPLEMENTED (compile+link-verified), VM-validate next.**
   Live A/B proved the flat byte-budget is wrong (1 MiB misses the ASLR-high heap → no capture; 4 MiB covers
   but a no-key MISS scans the whole region → catastrophically slow on the weak VM). Frontier research
   confirmed the fix is methodologically correct, not a guess: KeyReaper (DIMVA 2025) copies HEAP SEGMENTS only
   + structural key-schedule detection — exactly our design; PEB `NumberOfHeaps`@0xE8 / `ProcessHeaps`@0xF0
   (x64) are stable since NT 3.51. Now in code (`driver/capture.c`): the worker (attached) reads the PEB via
   `ZwQueryInformationProcess(ProcessBasicInformation).PebBaseAddress` (documented, avoids `PsGetProcessPeb`),
   SEH-reads `NumberOfHeaps`/`ProcessHeaps`, and snapshots each heap base's contiguous committed-private span
   (per-heap cap `SAR_CAPTURE_HEAP_PER_CAP` 1 MiB, total `SAR_CAPTURE_HEAP_BUDGET` 4 MiB, ≤`SAR_CAPTURE_HEAP_MAX`
   64 heaps); a wrong PEB offset degrades to a miss (SEH), never a crash. *VM-validate:* a CNG encryptor's key
   is captured at reasonable cost. **Residual to confirm in the VM:** whether CNG's `pbKeyObject=NULL`
   allocation lands in a PEB-enumerable heap (research 2-1 that PEB lists DLL-private heaps, likely yes) or a
   direct `VirtualAlloc` (then PEB-heap targeting misses it — measure, and only then widen).
   (c) **`MmIsAddressValid` resident-only behavior — VERIFIED.** The capture ran on the live write path with no
   bugcheck and no observable hang, i.e. no fatal paging re-entrancy on the filtered volume.
   (d) **`ZwQueryVirtualMemory` runtime — VERIFIED.** The `ZwCurrentProcess()` enumeration at the cached-write
   pre-op surfaced the originator's regions; the key was found and convicted.
   *DoD: capture half DONE* (CNG key captured despite per-write `BCryptDestroyKey`). *Remaining: recovery* —
   drive `SAR_CTL_OP_RECOVER` for the captured key_id+path and confirm the file is restored to plaintext
   (validates Unit 4 end-to-end in the live kernel). *Deps:* a control-pipe client.
1. **Keystore self-protection — Unit 3-Core: DONE (host core verified; kernel glue written,
   uncompiled).** Cross-reboot survival now exists on every platform: the keystore + MAC key +
   external anchor persist to disk, are verified and restored at boot, and rollback/erasure/tamper
   are caught by the anchor state machine. The constitutional premise was corrected: a Windows
   kernel driver has no TPM access, so VII.1.1/VII.1.2/VII.2.1 were rewritten (sealed-key unseal is
   the PPL service's job; on-disk confidentiality and MAC-key secrecy descend and are recorded
   where no TPM/VBS seal exists — VII.5). See `SLICE6_DESIGN.md`.
   *Remaining — Unit 3-Hardening (VM-bound, raises the recorded descent to hardware-rooted):*
   service-side TBS unseal of a TPM-sealed MAC key + IOCTL delivery to the kernel + application-PCR
   cap; a TPM-NV-counter or VBS-enclave anchor; on-disk AEAD encryption of the records; and the
   posture fields that report "rooted" vs "descended." *DoD:* on a TPM/VBS platform an attacker
   without kernel-code execution cannot read captured keys off disk and cannot forge the store; the
   bare-machine path keeps Core's recorded descent. *Deps:* Unit 3-Core (done); couples to Unit 5
   (PPL) for the boot-time unseal window. *Remaining for Unit 3-Core itself:* only the VM behavioral
   validations in `SLICE6_DESIGN.md` (boot-load timing, atomic-write power-loss, self-I/O skip) — it
   already compiles + links against the real WDK.
2. **Windows recovery wiring (Unit 4) — CORE DONE (host-verified + compile/link-verified, unrun).**
   The optimal split ("Design C", recorded in §3) is implemented end-to-end for the dominant
   full-file provenance case: operator → service control pipe (`SAR_CTL_OP_RECOVER {key_id, target}`)
   → `RECOVERY_EXEC {key_id, target_path}` over the authenticated comm port (send-recv, handshake-gated)
   → driver `SarRecoveryExecute` (`driver/recovery.c`): `SarKeystoreLookup` by key_id → open the
   on-disk ciphertext (`\??\<target>`) → **decrypt in kernel** with the freestanding engine
   (`sar_recover_buffer`, now compiled into the `.sys`) → write plaintext to `<target>.sarrectmp` →
   `RECOVERY_RESULT {status, bytes}` → service `sar_atomic_replace_file` does the metadata-preserving
   `ReplaceFileW`/POSIX-rename swap. **The captured key and the recovered plaintext never enter a
   user-mode process; the port carries only key_id + path + status** (Slice-1 "no keys over the port"
   contract is *preserved*, not revised — Design C made the revision unnecessary). Host-verified:
   `test_recover` 79/0 incl. the new `atomic_replace` test; service + control + engine build `/W4 /WX`;
   the driver compiles+links against the real WDK 26100 (`COMPILE_OK`/`LINK_OK`, `semantics_ar.sys`
   ≈121 KB) with `driver/recovery.c` + `engine/src/recover.c` added to the build.
   **Named follow-ons (deferred by budget, each boundary/DoD below; none half-edited):**
   (a) **large-file streaming** — the kernel reads the whole file into paged pool under a recorded
   `SAR_RECOVERY_MAX_BYTES` (64 MiB) cap; files over it return `SAR_RECOVER_DECLINED_TOO_LARGE` (no
   silent truncation). Constant-memory streaming needs a resumable engine decrypt (CBC/CFB chain across
   chunk boundaries — `sar_recover_buffer` references `ct[o-bs]`). *DoD:* multi-GB recovery in bounded
   memory. (b) **intermittent/partial-encryption geometry - DONE & VM-VERIFIED.** The record carries `provenance_length` (the convicting write's range); append dedups by `(key_id, provenance_path, provenance_offset)` so the keystore holds one record per encrypted region (whole-file = one region); recovery decrypts each region with `sar_recover_range` into an order-safe work buffer, verifies each region's tag, and writes back only verified regions - every other byte byte-identical. Family-agnostic (ranges = the actual convicting writes, not a per-family mapper; research: families/configs/keystream too varied + per-chunk nonce mutation e.g. Gentlemen). VM: `partial_encryptor` head-only victim3.txt -> keystore 0->810 (1 region), tail stays plaintext, recover -> whole file == golden. Host `test_recover` adds `recover_range_api`; Constitution II.5.1/III.1.2/III.4/VIII.2 generalized to regions. (c) **recovery verification before
   swap** — applying a wrong key/geometry yields a garbage temp; the core trusts the operator's
   key_id↔target pairing (safe for the proven provenance file). Persist a hash of the capture-time
   original sample `P` (a verification tag, not a recovery source → not preservation under III.1) in the
   keystore record (couples to capture) and have the kernel verify the decrypted head
   before producing the temp. *DoD:* a wrong key can never overwrite a file. (d) **volume enumeration /
   recover-all** — a VERDICT_NOTIFY-fed catalog (key_id→provenance paths) + ReFS/same-volume specifics +
   residue clearing. *Deps:* Units 2/3-Core/6 (done).
3. **PPL-AM / ELAM / MVI provisioning (Unit 5).** *Boundary:* the ELAM driver, MVI membership,
   and PPL-AM service launch that turn comm-port auth Layer B from "queried + recorded" into
   "enforced," and protect the service against injection (VII.4). *DoD:* the service runs PPL-AM
   where the platform supports it; absence still degrades to Layer A + recorded posture. *Deps:*
   none on the others; independently schedulable.
4. **Battery / recovery coverage closure (small, ongoing).** *Boundary:* the named gaps in the
   slice docs — Salsa20/XSalsa20 lack an external KAT (self-consistency only); AES-192 register-
   granularity inversion is best-effort; HC-128/SOSEMANUK are confirmed-limit demotions. *DoD:*
   each gap either closed with an independent vector or re-affirmed as a recorded confirmed
   limit. *Deps:* none.

5. **Periodic memory sampling of flagged encryptors — DONE & VM-VERIFIED (2026-06-26).** Implemented in
   `driver/capture.c` (+ `driver/driver.h` constants): a per-process behavioral flag raised on each
   capture-eligible destructive write (before the inflight cap, so cap-dropped writes are still covered) and a
   dedicated bounded-rate sampler system thread that re-attaches to flagged writers on a dual cadence —
   write-progress kick + bounded wall-clock revisit — re-snapshots committed-private memory (heaps first, then
   the rest: the snapshot was widened from PEB-heaps-only to a committed-private catch-all so CNG
   `pbKeyObject==NULL`/Segment-Heap allocations are covered), runs the structural detector, and convicts the
   resident key against the writer's most-recent stored (`P`,`C`) — feeding the existing `SarCaptureCommit`
   (keystore + ENFORCE block). Build: `cl /kernel` OK (`semantics_ar.sys` ~137 KB), host `ctest` 9/9.
   **VM-VERIFIED (live Win11 SarTarget):** (V2 stability) 500×8 burst ×2 + wipe, no bugcheck/hang, driver
   stays loaded, paged-pool 380 MB bounded — repeated `KeStackAttachProcess` on cadence is safe; (V1
   regression) `test_encryptor` → keystore 810 B → `sarctl recover` → SHA-256 == golden; (**GOAL** — ENFORCE,
   reused-key pre-write-wipe via `tests/harness/wipe_reuse.c`, key-alive hold 1500 ms) **damaged=1
   (recoverable) / prevented=49 / captured=1 = net permanent loss 0**, where the write-grab alone was TOTAL
   compromise — the sampler's wall-clock revisit caught the reused key in the inter-write window and the one
   conviction armed the block. **Honest scope:** the catch needs key *residence in a sample window* AND key
   *reuse* (a per-file key's only matching (`P`,`C`) is its own write, gone under pre-write-wipe); **per-file
   immediate-wipe of tiny files stays the irreducible VIII.2 floor.** Constitution amended clean
   (as-if-original): II.3.1/II.3.2 (Oracle re-cadenced: write snapshot + behavior-paced re-sample;
   committed-private region), II.4.2 (cumulative-N over writes AND resident-window samples), VI.1.1 (flag is
   behavioral, not identity), VIII.2 (the reused/macro-window pre-write-wipe is closed; per-file immediate-wipe
   is the floor), Part X checklist. Phase-2 dirtied-page delta scan deliberately NOT built — research showed no
   documented driver-callable Windows dirty-bit API; the documented checksum/VAD-delta proxy is the future
   optimization if full-rescan cost ever bites. *Original framing/research record retained below for context.*

   *Boundary (original):* in addition to the
   write-IRP capture, periodically sample a flagged encryptor's memory to catch the symmetric key
   during its live ENCRYPTION window — closing the "wipe the key before the write" evasion (II.3.2 /
   VIII.2) and catching short-lived per-file keys the write-grab can miss. **[LOAD-TEST EVIDENCE 2026-06-26 re-scopes this follow-up; the gap is narrower than first thought.]** A graduated VM load/stress test (scratchpad ransom_sim.c + splitter.c, full-file AES-256-CBC, hold=0) established: (i) STABILITY is excellent — no deadlock/hang/bugcheck, FS healthy, paged-pool peak +42MB at 500 files x 8 threads; (ii) in ENFORCE (the protective mode) net permanent data loss = 0 in ALL realistic scenarios — single fast process is blocked after ~2 files, and process-splitting (1/10/50/250 procs -> prevented 99.8/97/39/33%) evades the per-process block but NOT capture: Captured >= Damaged at every split level, so every damaged file stays recoverable (empirically vindicates VI.1); (iii) counterintuitively, splitting RAISES capture (single fast proc churns its heap -> write-grab misses, AUDIT 500=6.6%; split procs have low per-heap churn -> capture ~100%), so the attacker trades block-evasion for higher recoverability. So this unit's REAL residual narrows to two corners: (a) the extreme edge of a process doing ONE write then exiting in us (faster than the deferred snapshot) or wiping the key pre-snapshot (VIII.2), and (b) AUDIT-mode coverage where a single high-churn process loses keys the write-grab missed (no block to save it). Periodic memory sampling targets exactly (a)+(b) by catching the key during the live encryption window independent of write-grab timing. **Pre-write-wipe now MEASURED (2026-06-26, scratchpad/wipe_encryptor.c): a deliberate attacker that zeroes the key (BCryptDestroyKey + SecureZeroMemory of key object + raw key) BEFORE writing the ciphertext reduces the write-grab to 0% capture (AUDIT N=20/100), and in ENFORCE yields 0 prevented / 100 damaged / 0 captured = TOTAL compromise (no conviction -> no block -> no recovery); the process was kept alive 15s so the worker attached a LIVE process and still found nothing, isolating wipe from exit. So corner (a) is real and total, not hypothetical — and it is exactly the VIII.2 confirmed-limit, already declared by Part I. HONEST BOUND on the fix: periodic sampling's catch scales with the encryption-window DURATION — effective for reused keys + large files (MB-GB, the real ransomware target, long live window), near the irreducible floor for per-file + immediate-wipe + tiny files (us window); it degrades total->partial evasion, NOT to zero. It is defense-in-depth vs a sophisticated attacker (most ransomware write-then-free, which the write-grab already catches at 100% low-churn).** Frontier research validated
   this as a genuine, unfilled gap: every prior key-extraction system (KeyReaper DIMVA 2025, ShieldFS
   CryptoFinder, the USC/Napier/MSU forensic tools) does a SINGLE event/suspicion-triggered grab, none
   a cadence-driven sampler; the premise holds (>90% per-file Salsa20 key/nonce recovered from the
   live window and used to decrypt victim files without the master key); and the AES key schedule is a
   distinct ~100%-detectable structure (our `aes_schedule_scan` IS the CryptoFinder equivalent).
   **Why this, not crypto-API escrow:** no user-mode API hooking and no per-process injection, and —
   decisively — it survives the process-splitting evasion that collapses per-process behavioral
   classifiers from 98.6% to 0% (Naked Sun, ACNS 2020), because our flag is the **identity-independent
   gate** (D∧T per destructive write), which aggregates the cross-process effect (vindicates VI.1);
   escrow is bypassed outright by statically-linked/custom crypto, which memory sampling still catches.
   *Optimal design (concretized):* **(Phase 1, buildable on what exists)** the gate itself is the flag
   (a process's first D∧T-passing destructive write; on splitting, sample all recent D∧T originators —
   we already hold the referenced `PEPROCESS`); cadence is paced by **write-progress, not wall-clock**
   — sample every K destructive writes / K bytes destroyed (per ShieldFS's data-span ticks), so it
   auto-scales with attack throughput; the sample reuses the existing heap-targeted snapshot + structural
   detector under a periodic trigger. **(Phase 2, optimization)** a recently-DIRTIED-page delta scan
   (PTE Dirty bit / PFN modified / working-set age) so each sample scans only the delta where a fresh
   key schedule lands — no prior art does this; it is the genuine contribution. *DoD:* pre-write-wipe
   degraded from total evasion to partial/none at bounded cost. *Two empirical gates (VM-measure; no
   literature settles them):* (a) **cadence numbers** — realistic per-file encryption-window T_enc for
   KB-MB files vs feasible sample interval T_s; build the renewal/coverage model, measure the catch
   probability across an N-file campaign. (b) **delta mechanism** — whether a freshly-derived schedule
   reliably lands in a kernel-observable dirtied page, and whether per-process dirty-bit enumeration/clear
   is sound in kernel (undocumented/fragile — the load-bearing risk). *Residual (small, VIII.2):* tiny
   files zeroed faster than the sample interval; AES-NI/Key-Locker register-resident or non-precomputed
   schedules; enclaves. *Deps:* the capture path (item 0); independently schedulable after it.

## 5. Traps & non-obvious context

- **Never `FLT_PREOP_SYNCHRONIZE` an async write** — it deadlocks the modified-page writer. The
  paging flush runs at `APC_LEVEL` off the writer's thread; the live key is gone by then. This is
  the whole reason D1 holds the referenced originator at the *first* (cached) write.
- **Per-IRP requestor identity is unreliable** (worker-thread context under higher-filter IRP
  pending). Identity is used in exactly one place: the creation-time whitelist. Do not reach for
  the requestor PID anywhere in capture routing.
- **`ImageFileName` is spoofable** when `FileOpenNameAvailable == FALSE`; always take the canonical
  path from the `FileObject` + Flt name info.
- **Plaintext keys / candidates / `(P,C)` never cross the comm port** (Slice 1 contract). The seam
  hands references inside the kernel; the port carries only `key_id` + algorithm/mode/provenance.
- **Two-asset graduated response (Part I).** Oracle (definitive, unbounded — key-capture recovery)
  and Preservation (circumstantial, bounded — copy-on-first-write recovery) are both implemented.
  Key-capture cancels preservation for the recovered region (III.5.4 reconciliation, containment
  semantics). Preservation is the fallback for writes the Oracle cannot convict; it is not a
  backup of the keystore. A missed write, a dropped Oracle-input sample, a keyless wiper member:
  each is a confirmed limit absorbed by cumulative-N.
- **Altitude 385000 is a placeholder** — replace with the Microsoft-allocated integer before
  production signing.
- **Comm-port SD is at default ACL.** `SarBuildPortSecurity` uses
  `FltBuildDefaultSecurityDescriptor(FLT_PORT_ALL_ACCESS)`; the spec's "tighten so only the
  service SID has `FLT_PORT_CONNECT`" is deferred because the service SID is not fixed at this
  unit's scope. Runtime identity is still enforced in `ConnectNotify` (canonical image
  allow-list + signed challenge). Add the SID-restricted ACL once the service SID is known
  (install/PPL unit).
- **Open empirical items** (answer in WDK/VM, do not guess): `cldflt` hydrate-vs-write
  discrimination, ReFS integrity-stream / block-clone pre-image behavior, future non-cached-write
  BypassIO. Listed in `SLICE4_DESIGN.md`.

- **Recovery I/O is non-convicting by construction — no self-exemption code added, by design.** The
  kernel's temp write goes to a freshly created/overwritten file, so its pre-image is empty
  (`pre_image_len < SAR_CANDIDATE_SIZE`) and the worker never convicts; even if a stale temp gave a
  pre-image, plaintext-over-anything fails the forward proof (III.3.1). The service's `ReplaceFileW`
  rename-over-target is a `FileRename*` op, which `SarCaptureMemberCapturable` already excludes (capture
  is only `WRITE_CACHED`/`WRITE_NONCACHED`). Do **not** add a path/suffix-based recovery exemption — a
  `.sarrectmp`-suffix skip would be a capture-evasion hole.
- **`sar_recover_buffer` uses a function-`static` `ranges[SAR_MAX_RANGES]` (16 KiB) — not re-entrant.**
  Recovery is serialized in practice (single comm connection, `MaxConnections=1`, synchronous send-recv
  driven by the one-instance control pipe), so kernel recovery calls do not overlap. If recovery is ever
  parallelized, give the expansion a caller-provided buffer first.
- **Recovery target paths are operator/service-supplied Win32 absolute paths** (`C:\...`); the kernel
  prefixes `\??\`. The temp is `<target>.sarrectmp` derived identically on both sides, so the kernel-written
  temp and the service's `ReplaceFileW` replacement resolve to the same file. The path is trusted because
  the connecting service is authenticated (PPL query + signed-nonce handshake + image allow-list).
- **`SAR_RECOVERY_MAX_BYTES` (64 MiB) is a recorded design limit, not a silent cap** — over-cap files
  return `SAR_RECOVER_DECLINED_TOO_LARGE`. Streaming (constant memory) is the named follow-on; the cap is
  the in-kernel analogue of `SAR_KS_MAX_FILE`.

## 6. WDK-context verification checklist (driver compiles+links but is UNRUN; service still uncompiled)

> **Update (this session): the driver now COMPILES AND LINKS against the real WDK.** A WDK is
> present (`Windows Kits\10` 26100 km headers + libs + VS2022). All **26 object files** — every
> `driver/` TU + the borrowed `control/` + `engine/` battery/gate/keystore/sha256 + all 7 ciphers
> + the freestanding `capture/` core — compile clean with `cl /kernel` against the real
> `fltKernel.h`/`ntifs.h` at `NTDDI_WIN11_GE` (24H2; only `/W4` noise inside Microsoft's own
> headers) and **link into `semantics_ar.sys` (≈95 KB), `link` exit 0**. Bugs found and fixed by
> this build-out:
> - **(Unit 2)** mixed lookaside API (`NPAGED_LOOKASIDE_LIST` + `…Ex` calls → uniform
>   `LOOKASIDE_LIST_EX`); undeclared `PsGetThreadTeb` (Phase-2 cross-thread TEB read redesigned to
>   a Phase-1 `PsGetCurrentThreadTeb` stack snapshot in the originator's own context — better and
>   DDI-correct).
> - **(Unit 1 / Slice 4, were blocking the whole driver)** `FLT_REGISTRATION` had 2 extra
>   initializers — it has exactly 16 members and **no `SUPPORTED_FS_FEATURES_BYPASS_IO` field**
>   (that macro is an `ntifs.h` feature bit, not a registration member); removing it both compiles
>   and satisfies the Constitution IV.1.2 "do not declare `SUPPORTED_FS_FEATURES_BYPASS_IO`" (the
>   FSCTL-handler mediation in `bypassio.c` is unchanged). `PS_PROTECTION` / the
>   `SYSTEM_CODEINTEGRITY_INFORMATION` / `SYSTEM_ISOLATED_USER_MODE_INFORMATION` structs + their
>   info-class values + `ZwQuery{System,InformationProcess}` prototypes are **not in the WDK km
>   headers** (answering the §6 questions below): added as a new `driver/ntsystem.h` from verified
>   public definitions (`SystemCodeIntegrityInformation`=0x67, `…IsolatedUserMode…`=0xA5,
>   `CODEINTEGRITY_OPTION_HVCI_KMCI_ENABLED`=0x400, `PsProtectedSignerAntimalware`=3, the 16-byte
>   IUM struct). `NTDDI_VERSION` corrected `NTDDI_WIN10_RS3`→`NTDDI_WIN11_GE` (RS3 violated IX.1
>   and hid `ExAllocatePool2`/`FSCTL_MANAGE_BYPASS_IO`, used since Slice 4).
> - **(engine)** `cipher_common.h` MSVC `_rotl/_rotr/_rotl64` needed `#pragma intrinsic` so they
>   inline instead of emitting CRT calls unresolved under `/NODEFAULTLIB`.
>
> **Reproducible build recipe (until a CMake-WDK toolchain or EWDK is wired):** `cl /kernel /GS-`
> with `INCLUDE = <kit>\km;<kit>\km\crt;<VC+ucrt+shared>`, defines
> `_AMD64_ AMD64 _WIN64 NTDDI_VERSION=0x0A000010 _WIN32_WINNT=0x0A00 POOL_NX_OPTIN=1`, then
> `link /DRIVER /SUBSYSTEM:NATIVE,10.00 /ENTRY:DriverEntry /NODEFAULTLIB` against
> `fltMgr.lib ntoskrnl.lib hal.lib ksecdd.lib` (km\x64).
>
> **Caveats that are still NOT proven by a clean compile+link (do not overclaim):** (a) the
> hand-declared `ntsystem.h` struct *layouts* (esp. the 0x10-byte IUM struct) must be validated at
> runtime in a VM — a wrong size silently disables HVCI/VBS detection (degrades safe per VII.5, but
> is not "working"); (b) `driver/capture.c` and `capture/src/capture.c` both yield `capture.obj` —
> a CMake `MODULE` build needs distinct object names (the direct build used distinct `/Fo`); (c) no
> INF validation, test-signing, load, or behavioral test was done — those need a VM. Still run
> `InfVerif /h` + CodeQL Must-Fix and confirm the points below in a VM:

- **Preservation path (`driver/preserve.c` + `driver/store_io.c`) — VM-VERIFIED (2026-06-27, 24/24):**
  staging (FltReadFile cached pre-read from PreWrite, 16B–1MB), first-write-wins, Oracle reconciliation,
  ENFORCE capacity blocking (early gate-classify + would-exceed), restore round-trip (decrypt + content_tag
  verify + splice), self-protection of store files, budget control, persist/load with anchor verification
  across reboot (Phase 11: 10 entries survived, restore + new staging confirmed). **Remaining:** age
  eviction observation.
- **Recovery path (`driver/recovery.c`, Unit 4) — compiles+links into the `.sys`, UNRUN:** validate in a
  VM that `SarRecoveryExecute` opens `\??\<target>` and decrypts correctly end-to-end; that the kernel
  temp write + the service `ReplaceFileW`/POSIX-rename swap leave the recovered file with the target's
  DACL/ADS/object-id intact; that the recovery I/O is not self-convicted under ENFORCE; that concurrent
  capture and recovery do not deadlock on the keystore push lock (lookup takes it **shared**, never held
  across `ZwReadFile`/`ZwWriteFile` or `FltSendMessage`); and the held-open/POSIX-rename race against
  live ransomware on NTFS, ReFS, and SMB.
- **Capture path (`driver/capture.c`, Unit 2) — the new high-risk surface:**
  - `FltReadFile` pre-image read at the write pre-op: confirm it does not stall or invert FS
    locks (paging/main resource) per filesystem and write-type; confirm
    `FLTFL_IO_OPERATION_DO_NOT_UPDATE_BYTE_OFFSET` semantics and that a cached read returns the
    pre-image (not the just-written bytes).
  - `FltAllocateGenericWorkItem`/`FltQueueGenericWorkItem(... DelayedWorkQueue ...)`/
    `FltFreeGenericWorkItem` signatures, and that the self-managed `EX_RUNDOWN_REF`
    (acquire-at-submit / release-in-worker / `ExWaitForRundownProtectionRelease` at destroy)
    composes correctly with `FltUnregisterFilter`'s own rundown wait — destroy ordering in
    `SarFilterUnload` is capture-before-comm-before-unregister.
  - Synchronous heap snapshot (`SarCaptureSnapshotHeap`, replaces the old stack snapshot):
    `ZwQueryVirtualMemory(ZwCurrentProcess(), …, MemoryBasicInformation, …)` enumeration of
    committed-private RW regions, `MmIsAddressValid` per-page resident filter, SEH-guarded
    `RtlCopyMemory` into a `SAR_CAPTURE_HEAP_BUDGET` (256 KiB) paged buffer, gated to PASSIVE,
    taken in the originator thread context at the pre-op (no `KeStackAttachProcess`). Validate at
    runtime that (i) `MmIsAddressValid`+SEH never pages a valid-but-paged-out page in (no FS
    re-entrancy on the filtered volume), (ii) the low-address-first 256 KiB actually contains the
    CNG key object for representative ransomware (else raise the budget), and (iii) the synchronous
    snapshot latency added to the write path is acceptable. The deferred worker runs the engine
    structural detector + battery on the frozen buffer.
  - `FltSendMessage` with a relative timeout and `NULL` reply for the fire-and-forget
    `VERDICT_NOTIFY`; reconcile the header convention with `service/commclient.c`.
  - The engine battery + `capture` core compile **and link** into the `.sys` under
    `/NODEFAULTLIB` (proven this session — the `_rotl` CRT dependency was the only gap, fixed via
    `#pragma intrinsic`). `sar_gate_map_t` (8 KiB) and `sar_capture_result_t` are pool-allocated in
    `SarCaptureWorker`, never on the kernel stack.
  - **Register-grab lane (follow-on):** `KeSaveExtendedProcessorState` sized via
    `RtlGetEnabledExtendedFeatures`, IRQL `<= DISPATCH_LEVEL`.
- **BypassIO registration — ANSWERED + a surfaced Slice-4 defect (Unit 1, not Unit 2).** The
  26100 `FLT_REGISTRATION` has **exactly 16 members and no trailing `SupportedFeatures`/BypassIO
  field**, so `SUPPORTED_FS_FEATURES_BYPASS_IO` is **not** a `FLT_REGISTRATION` member — it must be
  a separate call. `driver/driver.c`'s `g_sar_registration` initializer therefore has one entry
  too many and fails to compile (`C2078: too many initializers`). **Fix (Unit 1):** drop the
  trailing `#if … SUPPORTED_FS_FEATURES_BYPASS_IO … #endif` initializer entry and register
  BypassIO support through its proper separate mechanism; reconcile the `FltVetoBypassIo`
  signature against `fltKernel.h` (26100). This blocks the full driver link until fixed.
- **VBS/HVCI detection structs — ANSWERED:** none of `SYSTEM_ISOLATED_USER_MODE_INFORMATION`,
  `SYSTEM_CODEINTEGRITY_INFORMATION`, their `SYSTEM_INFORMATION_CLASS` values, or
  `CODEINTEGRITY_OPTION_HVCI_KMCI_ENABLED` are in the 26100 km headers; declared in
  `driver/ntsystem.h` from verified public definitions. **Runtime-validate the IUM struct size
  (0x10) in a VM** — a mismatch silently fails VBS detection (`returned < sizeof` guard).
- **TPM presence:** the TBS/`TpmExtractEK`-based proxy in `feature.c` is heuristic — replace with a
  real TBS-presence check if available.
- **Process-notify enum:** the `PSCREATEPROCESSNOTIFYTYPE` constant passed to
  `PsSetCreateProcessNotifyRoutineEx2`.
- **`ZwQueryInformationProcess(ProcessProtectionInformation)` → `PS_PROTECTION` — ANSWERED:**
  `ProcessProtectionInformation` is in `ntddk.h`, but `PS_PROTECTION` / `PS_PROTECTED_SIGNER` /
  `ZwQueryInformationProcess` / `PROCESS_QUERY_LIMITED_INFORMATION` are **not** exposed to kernel
  callers in the 26100 headers; declared in `driver/ntsystem.h`. Runtime-validate that the PPL
  query succeeds and `PsProtectedSignerAntimalware` is read correctly in a VM.
- **Handshake signing primitive — RESOLVED + host-proven (this session).** Scheme is ECDSA-P256 over
  the raw 32-byte nonce, NULL padding, 64-byte P1363 signature; `tests/test_handshake_crypto.c`
  proves the `NCryptSignHash`↔`BCryptVerifySignature` interop and the 72-byte blob format. A real key
  pair is provisioned by `tools/provision_handshake_key.c` and the driver consumes
  `driver/service_pubkey.h` (no more placeholder). VM item reduces to: provision the **machine**-store
  key on the target and confirm the live port handshake end-to-end.
- **Comm-port reply framing:** reconcile the `FilterGetMessage`/`FilterSendMessage` /
  `FILTER_MESSAGE_HEADER` reply-buffer convention between `service/commclient.c` and
  `driver/commport.c` (whether our header is inline vs. behind the FltMgr reply header).
- **Port name** is now single-sourced in `common/.../protocol.h` (`SAR_COMM_PORT_NAME`); the
  driver/service duplicates were removed (Unit 6).
- **Service runtime validation (Unit 6 compiles+links + host-verified recovery core, but the live
  Win32 behavior is UNRUN — needs a VM with the driver loaded):** SCM start → `SERVICE_RUNNING`
  transition (with the deadlock-avoidance reorder); `FilterConnectCommunicationPort` to the live
  filter port; the ECDSA-P256 signed-challenge handshake against the driver over a real port (with a
  **real provisioned key pair** replacing the placeholder public-key blob in `driver/commport.c` and
  a matching KSP private key named `SemanticsArServiceKey`); the `FilterGetMessage`/`FilterSendMessage`
  framing against the live driver (Direction-A request = raw payload, Direction-B notify = app header
  after `FILTER_MESSAGE_HEADER`); the operator pipe with the restricted DACL; and end-to-end recovery
  on a real volume (the `ReplaceFileW`/`FileRenameInfoEx` writeback under power-loss and target-held-open
  conditions). PPL-AM launch (`ChangeServiceConfig2(SERVICE_CONFIG_LAUNCH_PROTECTED)`) compiles but is
  inert until the ELAM/signing unit (Unit 5).
