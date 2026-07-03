# Preservation Layer — Independent Design Review (Unadopted)

**Status:** Independent second-opinion review (Claude Fable 5), commissioned after the project
owner identified a structural flaw in the current preservation design (`CONSTITUTION.md` III.5.1
and III.5.2: whole-file capture at every write-intent open). Not yet reflected in
`CONSTITUTION.md` or implemented. This document preserves the review's argument, in full, for the
next implementer to evaluate and act on — it is the top-priority backend item, ahead of any
further frontend work (see `HANDOFF.md`).

## Why this review exists

The current backend captures the **entire file** once, at the write-intent **open**, via a
kernel-mode data-scan section, before any modification is applied (III.5.1). This is disproportionate
to the byte range actually destroyed: opening a large database file for a small in-place update
consumes preservation capacity for the whole file, unrelated to how much of it is ever written, and
— empirically confirmed this session — the capture runs *synchronously inside the post-create
callback* (`driver/capture.c`'s `SarCaptureSubmitOpenBaseline`, called from
`driver/operations.c`'s post-create handler), blocking the opening process's `CreateFile` call for
the duration of the whole-file copy. This was directly observed this session: redeploying a
100–200 KB test executable under rapid repeated rewrites intermittently locked the file
(`UnauthorizedAccessException` on `Remove-Item`/`WriteAllBytes`) until the driver was unloaded,
which forcibly released the in-flight capture. A large file would make this categorically worse,
and the disproportionate capacity consumption can also starve genuinely malicious captures of
preservation capacity if a large benign file is opened first.

The question under review: is whole-file-at-open the only deadlock-safe capture seam, as
originally concluded, or does a lazy, per-region alternative exist?

---

# Lazy, Per-Region, Deadlock-Free Preservation Without Redirect or Whole-File-at-Open

## 0. The question under review

The current backend captures the **entire file** once, at the write-intent **open**, via a
kernel-mode data-scan section, before any modification is applied. This was adopted because two
mechanisms were believed to be unsafe:

1. Reading the about-to-be-destroyed byte range synchronously *from inside* an ordinary
   `IRP_MJ_WRITE` was assumed to re-enter Cache Manager / Memory Manager locks already held by
   that same write, wedging the volume.
2. There is no IRP at the moment a memory-mapped page is dirtied by a CPU store; the only IRP
   visible to a filter is the later, asynchronous Modified Page Writer (MPW) flush, and
   synchronously intercepting that flush (`FLT_PREOP_SYNCHRONIZE` on an async write) is known to
   deadlock the MPW thread itself (it runs at `APC_LEVEL`, off the writer's own thread).

Given (1) and (2), "whole file, once, at open" was the only deadlock-safe seam identified, at the
cost of paying a full-file copy on *every* writable open of an existing file, regardless of file
size or whether the file is ever actually mapped writable.

**Verdict of this review: that conclusion is wrong.** Both constraints are real mechanisms, but
the perimeter drawn around them is too wide. A lazy, per-region design that touches neither the
write's destination nor the whole file exists, and covers every destruction path in scope
(`IV.1.2`: in-place overwrite, truncate/allocation-shrink, delete, mmap/section write,
rename-over-existing, hardlink-replace, block-clone/ODX).

---

## 1. Where the original analysis breaks

### 1.1 Ordinary writes are not the recursive/paging case

The claim "a synchronous same-stream read from inside the write path re-enters locks already
held by that write" is true **only for recursive and paging I/O**. It does not generalize to a
top-level user write.

A minifilter's pre-write callback for a normal cached, non-cached, or fast-I/O write runs at
`PASSIVE_LEVEL`, in the calling thread, **before** the underlying file system has acquired
anything. FltMgr sits above NTFS in the stack; NTFS acquires the FCB main/paging `ERESOURCE`s and
enters `CcCopyWrite` only *after* the pre-operation callback returns `FLT_PREOP_SUCCESS_WITH_CALLBACK`
or completes. At the moment the pre-op callback runs, the write holds **no** Cc or Mm locks for a
read to re-enter. A targeted read of the destination byte range at that point is safe, subject to
the guard set in §3.1.

This is exactly the seam the original analysis said did not exist.

### 1.2 mmap does not force capture at open — it forces capture at *writable section creation*

The claim "there is no IRP before an mmap'd page is dirtied" is true for the *store instruction*
itself, but the analysis conflated "no seam before the store" with "no seam before the file is
opened." There is an intermediate, much rarer, and much later event that *is* visible
synchronously, before any PTE for the view exists:

- `IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION`, with `Iopb->Parameters.AcquireForSectionSynchronization.SyncType == SyncTypeCreateSection`.

`MmCreateSection` acquires the file through the filter stack (an `FsRtlAcquireFileForCcFlushEx`-style
acquisition) before it creates the section object and before any page table entry for the mapped
view exists. This callback runs at `PASSIVE_LEVEL`, with no filesystem resources yet held by the
mapping thread. **Capture the file there — at writable-section-creation — not at open.**

A second, independent reduction applies at the same seam: filter on `PageProtection`.
`PAGE_WRITECOPY` sections — private copy-on-write mappings, which cover the overwhelming majority
of real `MapViewOfFile`/image-mapping usage (DLL/image maps, most read-mostly data maps) — **never
write back to the underlying file**; they can be skipped entirely. Only `PAGE_READWRITE` (and RWX)
*data* sections need capture. Writable file *opens* outnumber writable file *mappings* by orders
of magnitude in real workloads; this single filter removes most of the "every writable open pays a
full copy" cost before per-region capture is even applied.

---

## 2. The completeness argument

The reason whole-file-at-open felt necessary was the paging writeback path: by the time the MPW's
`IRP_PAGING_IO` write arrives, the in-memory page already holds the new content, and it is
correctly true that you cannot pend or re-enter the filesystem there (`APC_LEVEL`, MPW thread,
memory-pressure deadlock risk — all real constraints). The question this review asks instead is:
**where can a dirty user-data page's content have come from?**

There are exactly three origins:

1. **A top-level cached / non-cached / fast-I/O write.** Its pre-operation callback was seen, at
   `PASSIVE_LEVEL`, with the destination range known in advance (§1.1).
2. **An MDL write** (`IRP_MN_PREPARE_MDL_WRITE`, used by e.g. `srv2.sys` for SMB writes). The
   prepare-MDL pre-operation callback supplies offset and length *before* the caller memcpys new
   content into the cache pages — same capture point, same safety argument as (1).
3. **A CPU store into a writable mapped view** (including a remote `WriteProcessMemory` write into
   someone else's mapping). The section's creation was seen at the acquire-for-section-sync
   callback (§1.2), which is where the file was already captured.

(NTFS's own `CcSetDirtyPinnedData` metadata bookkeeping writes are not user data and are out of
scope, matching `IV.1.3`.)

**Every byte a paging write ever flushes to disk was already captured at one of these three
upstream seams.** The paging writeback IRP therefore is not the interception point of last resort
— it is redundant, and the filter can treat it as a no-op. This is the specific gap in the
original reasoning: it treated "no safe interception at paging-writeback time" as proof that whole-
file-at-open was required, when the paging writeback time was never actually load-bearing — the
two upstream seams already dominate it.

---

## 3. Concrete per-path design

### 3.1 In-place overwrite (cached / non-cached / fast-I/O / MDL write)

Maintain a per-stream interval tree of already-captured byte ranges. In the pre-write callback:
consult the tree; for any sub-range of `[offset, offset+length)` not yet captured, read the old
bytes for exactly that sub-range and record them (once), then mark the range captured; the real
write proceeds unmodified regardless.

**Guards — fall through *without* inline capture when any of these hold** (this is safe precisely
because of the completeness argument in §2: these are exactly the cases already covered upstream):

- `Data->Iopb->IrpFlags & (IRP_PAGING_IO | IRP_SYNCHRONOUS_PAGING_IO)`
- `KeGetCurrentIrql() > PASSIVE_LEVEL`
- `IoGetTopLevelIrp() != NULL` (lazy-writer, recursive FS-internal I/O, or the filter's own I/O)
- I/O the filter itself issued (tag via an ECP or route through a distinguished instance)

For the read of the old bytes, do not perform cached I/O on the caller's own file object (that
would be the same re-entrant hazard, just relocated). Two safe options:

- **(a)** A non-cached read on a separate shadow file object for the same stream, opened once via
  `FltCreateFileEx2`, targeted *below* the capturing instance, sector-aligned to the destination
  range. This returns on-disk state, which may lag dirty-but-unflushed cache content.
- **(b, preferred default)** `FltCreateSectionForDataScan` on the stream and `memcpy` from the
  mapped view. Section pages share the file's `DataSectionObject` prototype PTEs with the Cache
  Manager, so the read observes current logical content (including dirty-but-not-yet-flushed
  data), and a resident page costs a memcpy rather than a disk read. This API exists specifically
  for this class of out-of-band, deadlock-free capture.

### 3.2 Truncate / allocation shrink

Pre-`IRP_MJ_SET_INFORMATION` for `FileEndOfFileInformation`, `FileAllocationInformation`, and
`FileValidDataLengthInformation`: capture `[newEOF, oldEOF)` only. Skip `AdvanceOnly` EOF sets —
these are the Cache Manager's own internal lazy-writer EOF advances, not destruction, and not a
safe capture context.

### 3.3 Zeroing / trim / offload-write / block-clone-over

Pre-`IRP_MJ_FILE_SYSTEM_CONTROL` for `FSCTL_SET_ZERO_DATA`, `FSCTL_FILE_LEVEL_TRIM`,
`FSCTL_OFFLOAD_WRITE` (an ODX token write destroys the target range), and
`FSCTL_DUPLICATE_EXTENTS_TO_FILE` (destroys the *target* file's range). Each FSCTL's input buffer
names the exact byte range up front; capture exactly that range. All of these arrive at
`PASSIVE_LEVEL` with nothing held.

### 3.4 Superseding create (`FILE_SUPERSEDE` / `FILE_OVERWRITE(_IF)`)

This disposition destroys the whole file on open by construction, so whole-file capture *here* is
proportional to the actual destruction, not wasted work — it is also the classic
delete-and-recreate ransomware pattern. Open a distinct handle in pre-create and capture the whole
prior content there (or use the hardlink trick of §3.5 if the goal is preservation rather than a
readable copy).

### 3.5 Delete / rename-over-existing / hardlink-replace

These destroy a **name → data binding**, not necessarily the data itself, and on NTFS/ReFS this
can be preserved in **O(1)**, with no data copy at all: create a hardlink into a protected, ACL'd
quarantine directory.

- For delete: at pre-set-disposition (`FileDispositionInformation(Ex)`), before the delete bit is
  set — link creation on NTFS fails once a stream is `DELETE_PENDING`, so the link must be created
  first — or at pre-create for `FILE_DELETE_ON_CLOSE`, issue
  `FltSetInformationFile(FileLinkInformation)` on a targeted handle into the quarantine directory.
  The link count goes to 2; the subsequent delete removes one name; the on-disk clusters survive
  untouched under the surviving link.
- The same trick applies to the doomed *target* of `FileRenameInformation(Ex)` with
  `ReplaceIfExists`, and of `FileLinkInformation` with `ReplaceIfExists`: hardlink the
  about-to-be-replaced target into quarantine before the replace commits.

**Caveat:** a hardlink shares the underlying stream, so it protects only the *namespace* axis. If
the same file is *also* overwritten in place (rather than purely deleted/replaced), the per-region
write capture of §3.1 handles that axis independently and must still run. This trick has no
equivalent on FAT/exFAT (no hardlinks) — fall back to a copy there.

### 3.6 mmap / writable section

Capture the whole file (or, better, only the regions later dirtied — see the residual-latency
note in §5) at `IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION` / `SyncTypeCreateSection`, filtered to
`PAGE_READWRITE`/RWX data sections only (§1.2). This is the one path where whole-file capture is
retained — but it now fires only on the rare event of a genuinely writable data mapping, not on
every writable open.

### 3.7 Cost model

Preservation cost becomes **proportional to bytes actually destroyed**, not to file size:

- A 40 GB VHDX opened for a 64 KB in-place update costs one 64 KB capture, not a 40 GB scan.
- A full-file in-place ransomware encryption pass degenerates to reading the whole file — but
  interleaved with the attack's own writes, and it is the *same total I/O* the current design
  already pays, just paid exactly when destruction happens rather than eagerly at open.
- Appends strictly beyond EOF destroy nothing and cost nothing.
- Writable opens that are never actually mapped writable, and never actually written to, now cost
  nothing (versus a full copy today).

---

## 4. The three sub-questions raised in review

### 4.1 Page-fault-level / PTE-protection tricks for the mmap case

**Agree with the existing war-story-based rejection of this approach — do not pursue it.** A
third-party driver has no supported way to receive another process's page faults for a file-backed
mapping. User PTEs for shared file mappings are materialized on demand from **prototype PTEs**
owned by the section object; the Memory Manager can trim a resident page and rebuild its PTE at
any time without preserving any protection bit a driver installed, silently defeating the trap.
Concurrent faults on other CPUs racing a single store are unwinnable without owning the fault
handler outright. User-mode equivalents (`PAGE_GUARD` + an injected vectored exception handler)
are one-shot per fault, racy across threads (two threads storing to the same page concurrently can
let one write through untrapped), trivially defeated by the process being monitored, and
uninjectable into PPL/CIG-protected processes. `MEM_WRITE_WATCH` / `GetWriteWatch` is a dead end
for this purpose: it applies only to `MEM_RESERVE | MEM_WRITE_WATCH` **private** allocations made
by the *owning* process; it cannot be attached to a file-backed section, to another process's
mapping, or retroactively, and even where applicable it reports only *that* a page was dirtied, not
its pre-image. The only mechanism that robustly traps writes below the OS's own memory-management
data structures is EPT/second-level-address-translation permission control from a driver's own
hypervisor (the approach Bromium-lineage products used) — technically real, but a product-scale
undertaking with severe compatibility cost against Hyper-V, VBS, and HVCI, and out of scope for a
minifilter-based driver. Capture-at-writable-section-creation (§1.2, §3.6) is the correct answer
for mmap; accept that it remains whole-file for that specific, now-rare case.

### 4.2 Filesystem / volume-level CoW primitives

**Use these as opportunistic accelerators and backstops, not as the primary architecture.**

- **ReFS block cloning** (`FSCTL_DUPLICATE_EXTENTS_TO_FILE`): on a ReFS volume (including a Dev
  Drive), "capture" can become an O(metadata) allocate-on-write clone of the target file or range —
  flush dirty data first (`FltFlushBuffers`), then clone into the quarantine location. This makes
  even the whole-file mmap-path capture of §3.6 nearly free. It does not exist on NTFS, so it is an
  opportunistic fast path conditioned on volume format, never the baseline design.
- **VSS (Volume Shadow Copy Service):** composes cleanly with a live minifilter gate — `volsnap`
  is a volume-level filter below the file system, with no interaction at this driver's altitude —
  and its 16 KB-block copy-on-write is exactly the lazy, deadlock-free preservation semantics being
  sought. It fails as the *primary* mechanism for three concrete reasons: (1) snapshot creation is
  heavyweight and effectively serialized (a flush-and-hold operation costing hundreds of
  milliseconds; it cannot be triggered per-event), (2) ransomware routinely deletes shadow copies
  as an early step, so shadow-copy deletion (`IOCTL_VOLSNAP...`, `vssadmin`) must be actively
  intercepted and blocked — doable, and something EDR products already do, but an added
  responsibility, and (3), the most underrated failure mode: sustained full-volume encryption
  churn exhausts the VSS diff area, at which point `volsnap` **silently evicts the oldest
  snapshots** — the mechanism self-destructs under precisely the workload it exists to defend
  against. Recommendation: run periodically-refreshed, deletion-protected VSS snapshots as a
  backstop for the residual mmap-window and for catastrophic/volume-scale scenarios, generously
  sized; never rely on VSS as the primary per-write preservation mechanism.

### 4.3 Is there a genuinely different architecture available?

Yes — the one described in §§1–3: **asymmetric** capture. Cheap, lazy, per-region capture for
every path with an IRP-visible pre-destruction seam (§3.1–§3.5), whole-file capture retained only
for the one path that structurally requires it and made rare by relocating it from open to
writable-section-creation (§3.6), with format-conditioned acceleration (ReFS clone) and a
volume-level backstop (protected VSS) layered on top. None of this requires the gate to decide, at
write time, whether an operation is redirected, blocked, or altered — the gate invariant (`IV.3.1`)
is untouched throughout; every mechanism above is a passive, out-of-band read or an O(1) namespace
operation that runs alongside the unmodified real write.

---

## 5. Residual caveats (stated plainly, not smoothed over)

- **First-write latency.** The first touch of a cold, previously-uncaptured region adds a
  synchronous read to that write's completion path. A warm/cached region is a memcpy via the
  data-scan section rather than a disk read. This is the price of proportionality; it is bounded
  and paid at most once per (stream, region).
- **Restore semantics under interleaved writers.** Per-region capture assembles a restore image
  from regions captured at slightly different real times — it is not a single crash-consistent
  point-in-time snapshot. For the dominant threat model (one malicious writer executing an
  encryption pass) this reconstructs the exact pre-attack content region by region. For a file with
  *both* a benign writer and a malicious writer interleaved, the reconstructed "original" is
  ambiguous in the same way the current whole-file-at-open design is ambiguous (restoring there
  discards any benign edits made after the open) — this is not a regression introduced by the new
  design, it is the same pre-existing ambiguity in a different shape.
- **State and placement.** The per-stream interval tree and the captured-bytes store are new
  driver state; writes into the store re-enter the filter stack and must be tagged and routed below
  the capturing instance (as the existing rename/hardlink-destination capture already does).
  Capacity must be bounded, matching the existing III.5.5 pool/reclamation model.
- **Alternate Data Streams.** Capture keys must be `(file, stream)`, not `(file)` alone — each ADS
  is captured independently.
- **Boot-ordering blind spot.** A section created before this filter attaches to the volume is a
  blind spot in *both* the current design and this one; it is worth a deliberate, explicit
  scan-on-attach policy decision rather than an implicit gap.

---

## 6. Bottom line

The original conclusion — "whole-file-at-open is the only deadlock-safe seam, therefore every
writable open pays a full-file copy" — over-generalized two real constraints (no safe re-entrant
read inside a *recursive/paging* write; no safe synchronous interception of the *paging writeback*
IRP) into a claim about *all* writes and *all* mmap activity. Neither constraint actually forces
whole-file-at-open:

1. An ordinary top-level write's pre-operation callback runs before the file system holds any
   lock, making a targeted, per-region synchronous read of just the destination range safe.
2. The relevant mmap seam is writable-section-*creation*, not the file open, and it can be
   narrowed further by skipping `PAGE_WRITECOPY` sections outright.
3. Every byte a paging write ever flushes originated at one of the two seams above, so the
   paging-writeback IRP — the one genuinely unsafe interception point — never needs to be
   intercepted at all.

The corrected architecture is asymmetric, lazy, and proportional to bytes actually destroyed
rather than to file size, it requires no redirection of any write's destination, and it leaves the
gate's `IV.3.1` passivity invariant completely untouched. Whole-file capture survives only as the
fallback for genuinely-writable memory mappings (now a rare event, not a per-open tax) and as the
proportional response to a whole-file-destroying create disposition — not as the default cost of
every writable open.
