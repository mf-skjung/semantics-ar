#include "capture.h"
#include "state.h"
#include "commport.h"
#include "keystore_persist.h"
#include "preserve.h"
#include "ntsystem.h"

#include <ntifs.h>

#include "sar_capture.h"
#include "aes.h"

extern PSAR_STATE g_sar_state;

typedef struct _SAR_FLAG_SLOT {
    PEPROCESS process;
    UCHAR pre_image[SAR_CAPTURE_BUFFER_BYTES];
    ULONG pre_image_len;
    UCHAR written[SAR_CAPTURE_BUFFER_BYTES];
    ULONG written_len;
    UINT64 file_offset;
    UINT64 provenance_offset;
    ULONG write_length;
    UINT16 provenance_path[SEMANTICS_AR_PROVENANCE_PATH_MAX];
    volatile LONG64 progress_bytes;
    LARGE_INTEGER last_sample_time;
    LARGE_INTEGER last_write_time;
    LARGE_INTEGER last_pc_refresh;
} SAR_FLAG_SLOT, *PSAR_FLAG_SLOT;

struct _SAR_CAPTURE_CTX {
    PFLT_FILTER filter;

    LOOKASIDE_LIST_EX work_pool;
    BOOLEAN work_pool_init;

    EX_PUSH_LOCK blocked_lock;
    PEPROCESS *blocked;
    ULONG blocked_count;

    EX_RUNDOWN_REF rundown;
    volatile LONG inflight;

    volatile LONG64 whitelist_skips;
    volatile LONG64 pressure_drops;
    volatile LONG64 preload_drops;

    EX_PUSH_LOCK flag_lock;
    PSAR_FLAG_SLOT flags;
    ULONG flag_count;

    PVOID sampler_thread;
    KEVENT sampler_wake;
    volatile LONG sampler_stop;
    PUCHAR sampler_buf;
    sar_gate_map_t *sampler_map;
    sar_capture_result_t *sampler_result;
};

typedef struct _SAR_CAPTURE_WORK {
    PSAR_CAPTURE_CTX ctx;
    PFLT_GENERIC_WORKITEM work_item;
    PEPROCESS originator_process;
    PETHREAD originator_thread;
    UINT64 file_offset;
    ULONG write_length;
    ULONG pre_image_len;
    ULONG written_len;
    ULONG scan_len;
    PUCHAR scan;
    UCHAR pre_image[SAR_CAPTURE_BUFFER_BYTES];
    UCHAR written[SAR_CAPTURE_BUFFER_BYTES];
    UINT16 provenance_path[SEMANTICS_AR_PROVENANCE_PATH_MAX];
    UINT64 provenance_offset;
    PUCHAR preserve_buf;
    ULONG preserve_len;
} SAR_CAPTURE_WORK, *PSAR_CAPTURE_WORK;

_IRQL_requires_max_(APC_LEVEL)
static BOOLEAN SarCaptureMemberCapturable(_In_ sar_destruct_member_t Member)
{
    return Member == SAR_DESTRUCT_WRITE_CACHED || Member == SAR_DESTRUCT_WRITE_NONCACHED;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static VOID SarCaptureResolveProvenance(_In_ PFLT_CALLBACK_DATA Data,
                                        _Out_writes_(SEMANTICS_AR_PROVENANCE_PATH_MAX) PUINT16 Out)
{
    PFLT_FILE_NAME_INFORMATION info = NULL;
    NTSTATUS status;
    ULONG i;

    for (i = 0; i < SEMANTICS_AR_PROVENANCE_PATH_MAX; i++)
        Out[i] = 0;

    status = FltGetFileNameInformation(Data,
                                       FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT,
                                       &info);
    if (!NT_SUCCESS(status))
        return;

    if (NT_SUCCESS(FltParseFileNameInformation(info))) {
        ULONG chars = info->Name.Length / sizeof(WCHAR);
        const WCHAR *src = info->Name.Buffer;
        if (chars > SEMANTICS_AR_PROVENANCE_PATH_MAX - 1)
            chars = SEMANTICS_AR_PROVENANCE_PATH_MAX - 1;
        for (i = 0; i < chars; i++)
            Out[i] = (UINT16)src[i];
        Out[chars] = 0;
    }

    FltReleaseFileNameInformation(info);
}

_IRQL_requires_max_(APC_LEVEL)
static ULONG SarCaptureCopyWritten(_In_ PFLT_CALLBACK_DATA Data,
                                   _Out_writes_bytes_(Cap) PUCHAR Buf, _In_ ULONG Cap)
{
    PVOID src = NULL;
    ULONG len = Data->Iopb->Parameters.Write.Length;
    ULONG copy;

    if (len == 0)
        return 0;
    copy = len < Cap ? len : Cap;

    if (Data->Iopb->Parameters.Write.MdlAddress != NULL)
        src = MmGetSystemAddressForMdlSafe(Data->Iopb->Parameters.Write.MdlAddress,
                                           NormalPagePriority | MdlMappingNoExecute);
    else
        src = Data->Iopb->Parameters.Write.WriteBuffer;

    if (src == NULL)
        return 0;

    __try {
        RtlCopyMemory(Buf, src, copy);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return copy;
}

#define SAR_PEB_NUMBEROFHEAPS 0xE8
#define SAR_PEB_PROCESSHEAPS  0xF0

_IRQL_requires_(PASSIVE_LEVEL)
static ULONG SarCaptureCopyResident(_In_ ULONG_PTR Base, _In_ ULONG_PTR Size,
                                    _Out_writes_bytes_(Cap) PUCHAR Buf, _In_ ULONG Cap)
{
    ULONG_PTR p;
    ULONG total = 0;

    for (p = Base; p < Base + Size && total < Cap; p += PAGE_SIZE) {
        ULONG want = (Cap - total < PAGE_SIZE) ? (Cap - total) : PAGE_SIZE;

        if (!MmIsAddressValid((PVOID)p))
            continue;

        __try {
            RtlCopyMemory(Buf + total, (PVOID)p, want);
            total += want;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            /* page raced out between probe and copy; skip it */
        }
    }
    return total;
}

_IRQL_requires_(PASSIVE_LEVEL)
static ULONG SarCaptureSnapshotHeap(_In_ HANDLE ProcessHandle, _In_ ULONG_PTR HeapBase,
                                    _Out_writes_bytes_(Cap) PUCHAR Buf, _In_ ULONG Cap)
{
    MEMORY_BASIC_INFORMATION mbi;
    ULONG_PTR addr = HeapBase;
    ULONG total = 0;
    SIZE_T ret;

    while (total < Cap &&
           NT_SUCCESS(ZwQueryVirtualMemory(ProcessHandle, (PVOID)addr, MemoryBasicInformation,
                                           &mbi, sizeof(mbi), &ret))) {
        ULONG_PTR base = (ULONG_PTR)mbi.BaseAddress;
        ULONG_PTR size = (ULONG_PTR)mbi.RegionSize;
        ULONG_PTR next = base + size;

        if (size == 0 || next <= addr)
            break;
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE ||
            (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) == 0 ||
            (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            break;

        total += SarCaptureCopyResident(base, size, Buf + total, Cap - total);
        addr = next;
    }
    return total;
}

_IRQL_requires_(PASSIVE_LEVEL)
static ULONG SarCaptureSnapshotProcess(_In_ HANDLE ProcessHandle, _In_ PVOID Peb,
                                       _Out_writes_bytes_(Cap) PUCHAR Buf, _In_ ULONG Cap)
{
    ULONG_PTR heapbases[SAR_CAPTURE_HEAP_MAX];
    ULONG nheaps = 0;
    ULONG total = 0;
    MEMORY_BASIC_INFORMATION mbi;
    ULONG_PTR addr;
    SIZE_T ret;
    ULONG i;

    __try {
        ULONG num = *(ULONG *)((PUCHAR)Peb + SAR_PEB_NUMBEROFHEAPS);
        PVOID *heaps = *(PVOID **)((PUCHAR)Peb + SAR_PEB_PROCESSHEAPS);

        if (heaps != NULL) {
            if (num > SAR_CAPTURE_HEAP_MAX)
                num = SAR_CAPTURE_HEAP_MAX;
            for (i = 0; i < num; i++) {
                if (heaps[i] != NULL)
                    heapbases[nheaps++] = (ULONG_PTR)heaps[i];
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        nheaps = 0;
    }

    for (i = 0; i < nheaps && total < Cap; i++) {
        ULONG remain = Cap - total;
        ULONG perheap = remain < SAR_CAPTURE_HEAP_PER_CAP ? remain : SAR_CAPTURE_HEAP_PER_CAP;
        total += SarCaptureSnapshotHeap(ProcessHandle, heapbases[i], Buf + total, perheap);
    }

    addr = 0;
    while (total < Cap &&
           NT_SUCCESS(ZwQueryVirtualMemory(ProcessHandle, (PVOID)addr, MemoryBasicInformation,
                                           &mbi, sizeof(mbi), &ret))) {
        ULONG_PTR base = (ULONG_PTR)mbi.BaseAddress;
        ULONG_PTR size = (ULONG_PTR)mbi.RegionSize;
        ULONG_PTR next = base + size;

        if (size == 0 || next <= addr)
            break;

        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
            (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) != 0 &&
            (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0) {
            BOOLEAN isheap = FALSE;

            for (i = 0; i < nheaps; i++) {
                if (heapbases[i] >= base && heapbases[i] < next) {
                    isheap = TRUE;
                    break;
                }
            }
            if (!isheap) {
                ULONG remain = Cap - total;
                ULONG perregion = remain < SAR_CAPTURE_HEAP_PER_CAP ? remain : SAR_CAPTURE_HEAP_PER_CAP;
                total += SarCaptureCopyResident(base, size, Buf + total, perregion);
            }
        }
        addr = next;
    }
    return total;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static VOID SarCaptureSendNotify(_In_ PSAR_CAPTURE_CTX Ctx,
                                 _In_ const semantics_ar_verdict_notify_t *Notify)
{
    PSAR_COMM comm = (PSAR_COMM)g_sar.comm;
    LARGE_INTEGER timeout;

    if (comm == NULL || comm->client_port == NULL)
        return;

    timeout.QuadPart = -50000000;
    FltSendMessage(Ctx->filter, &comm->client_port, (PVOID)Notify,
                   sizeof(*Notify), NULL, NULL, &timeout);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static VOID SarCaptureBlockOriginator(_In_ PSAR_CAPTURE_CTX Ctx, _In_ PEPROCESS Originator)
{
    ULONG i;
    BOOLEAN present = FALSE;

    FltAcquirePushLockExclusive(&Ctx->blocked_lock);
    for (i = 0; i < Ctx->blocked_count; i++) {
        if (Ctx->blocked[i] == Originator) {
            present = TRUE;
            break;
        }
    }
    if (!present && Ctx->blocked_count < SAR_BLOCKED_CAPACITY) {
        ObReferenceObject(Originator);
        Ctx->blocked[Ctx->blocked_count] = Originator;
        Ctx->blocked_count++;
    }
    FltReleasePushLock(&Ctx->blocked_lock);
}

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarCaptureOriginatorBlocked(_In_opt_ PSAR_CAPTURE_CTX Ctx, _In_ PEPROCESS Originator)
{
    ULONG i;
    BOOLEAN found = FALSE;

    if (Ctx == NULL)
        return FALSE;

    FltAcquirePushLockShared(&Ctx->blocked_lock);
    for (i = 0; i < Ctx->blocked_count; i++) {
        if (Ctx->blocked[i] == Originator) {
            found = TRUE;
            break;
        }
    }
    FltReleasePushLock(&Ctx->blocked_lock);
    return found;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static VOID SarCaptureCommit(_In_ PSAR_CAPTURE_CTX Ctx, _In_ sar_capture_result_t *Result,
                             _In_ PEPROCESS Originator)
{
    if (SarKeystoreAppend(g_sar.keystore, &Result->record) == 1)
        SarCaptureSendNotify(Ctx, &Result->notify);

    SarPreserveReconcile(g_sar.preserve, Result->record.provenance_path,
                         Result->record.provenance_offset, Result->record.provenance_length);

    if (SarStateModeGet(g_sar_state) == SEMANTICS_AR_MODE_ENFORCE)
        SarCaptureBlockOriginator(Ctx, Originator);
}

_IRQL_requires_(PASSIVE_LEVEL)
static VOID SarCapturePreserveFromWork(_In_ PSAR_CAPTURE_CTX Ctx, _In_ PSAR_CAPTURE_WORK Work)
{
    SIZE_T pathchars;
    ULONG validate_len;

    if (g_sar.preserve == NULL || !SarPreserveReady(g_sar.preserve))
        return;
    if (Work->write_length == 0 || Work->write_length > SAR_PRESERVE_STAGE_MAX)
        return;

    if (SarStateModeGet(g_sar_state) == SEMANTICS_AR_MODE_ENFORCE &&
        SarPreserveWouldExceed(g_sar.preserve, Work->write_length)) {
        SarCaptureBlockOriginator(Ctx, Work->originator_process);
        return;
    }

    if (Work->preserve_buf == NULL || Work->preserve_len < Work->write_length)
        return;

    for (pathchars = 0; pathchars < SEMANTICS_AR_PROVENANCE_PATH_MAX; pathchars++) {
        if (Work->provenance_path[pathchars] == 0)
            break;
    }
    if (pathchars == 0)
        return;

    validate_len = Work->pre_image_len < Work->write_length ? Work->pre_image_len :
                   Work->write_length;
    if (validate_len > SAR_CAPTURE_BUFFER_BYTES)
        validate_len = SAR_CAPTURE_BUFFER_BYTES;

    if (validate_len > 0 &&
        RtlEqualMemory(Work->preserve_buf, Work->pre_image, validate_len)) {
        SarPreserveStage(g_sar.preserve, Work->provenance_path, Work->file_offset,
                         Work->write_length, Work->preserve_buf, Work->write_length);
    }
}

_Function_class_(FLT_GENERIC_WORKITEM_ROUTINE)
_IRQL_requires_max_(PASSIVE_LEVEL)
static VOID SarCaptureWorker(_In_ PFLT_GENERIC_WORKITEM FltWorkItem, _In_ PVOID FltObject,
                            _In_opt_ PVOID Context)
{
    PSAR_CAPTURE_WORK work = (PSAR_CAPTURE_WORK)Context;
    PSAR_CAPTURE_CTX ctx;
    sar_gate_map_t *map;
    sar_capture_result_t *result;

    UNREFERENCED_PARAMETER(FltObject);

    ctx = work->ctx;
    map = (sar_gate_map_t *)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*map), SAR_POOL_TAG_GATEMAP);
    result = (sar_capture_result_t *)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*result),
                                                     SAR_POOL_TAG_CAPRES);

    if (map != NULL && result != NULL && work->pre_image_len >= SAR_CANDIDATE_SIZE &&
        work->written_len >= SAR_CANDIDATE_SIZE) {
        sar_capture_request_t req;
        ULONG sample = work->pre_image_len < work->written_len ?
                       work->pre_image_len : work->written_len;
        sar_gate_result_t gate;

        sar_gate_classify(map, work->pre_image, work->written, sample, &gate);
        if (gate.candidate &&
            SarStateModeGet(g_sar_state) == SEMANTICS_AR_MODE_ENFORCE &&
            g_sar.preserve != NULL && SarPreserveReady(g_sar.preserve) &&
            work->write_length > 0 && work->write_length <= SAR_PRESERVE_STAGE_MAX &&
            SarPreserveWouldExceed(g_sar.preserve, work->write_length)) {
            SarCaptureBlockOriginator(ctx, work->originator_process);
        }
        if (gate.candidate) {
            work->scan = (PUCHAR)ExAllocatePool2(POOL_FLAG_PAGED, SAR_CAPTURE_HEAP_BUDGET,
                                                 SAR_POOL_TAG_SNAPSHOT);
        }
        if (work->scan != NULL) {
            HANDLE hproc;
            if (NT_SUCCESS(ObOpenObjectByPointer(work->originator_process, OBJ_KERNEL_HANDLE, NULL,
                                                 0, *PsProcessType, KernelMode, &hproc))) {
                if (NT_SUCCESS(PsAcquireProcessExitSynchronization(work->originator_process))) {
                    KAPC_STATE apc;
                    SAR_PROCESS_BASIC_INFORMATION pbi;
                    ULONG pret = 0;

                    KeStackAttachProcess(work->originator_process, &apc);
                    if (NT_SUCCESS(ZwQueryInformationProcess(hproc, ProcessBasicInformation, &pbi,
                                                             sizeof(pbi), &pret)) &&
                        pbi.PebBaseAddress != NULL)
                        work->scan_len = SarCaptureSnapshotProcess(hproc, pbi.PebBaseAddress,
                                                                   work->scan, SAR_CAPTURE_HEAP_BUDGET);
                    KeUnstackDetachProcess(&apc);
                    PsReleaseProcessExitSynchronization(work->originator_process);
                }
                ZwClose(hproc);
            }
        }

        req.plaintext = work->pre_image;
        req.ciphertext = work->written;
        req.sample_size = sample;
        req.file_offset = work->file_offset;
        req.candidates = NULL;
        req.candidate_count = 0;
        req.scan_buffer = work->scan;
        req.scan_length = work->scan_len;
        req.provenance_path = work->provenance_path;
        req.provenance_offset = work->provenance_offset;
        req.provenance_length = work->write_length;

        if (sar_capture_run(&req, map, SarKeystoreMacKey(g_sar.keystore), result) ==
            SAR_CAPTURE_CONVICTED) {
            SarCaptureCommit(ctx, result, work->originator_process);
        } else if (gate.candidate) {
            SarCapturePreserveFromWork(ctx, work);
        }
    }

    if (map != NULL)
        ExFreePoolWithTag(map, SAR_POOL_TAG_GATEMAP);
    if (result != NULL) {
        RtlSecureZeroMemory(result, sizeof(*result));
        ExFreePoolWithTag(result, SAR_POOL_TAG_CAPRES);
    }

    RtlSecureZeroMemory(work->pre_image, sizeof(work->pre_image));
    RtlSecureZeroMemory(work->written, sizeof(work->written));
    if (work->scan != NULL) {
        RtlSecureZeroMemory(work->scan, work->scan_len);
        ExFreePoolWithTag(work->scan, SAR_POOL_TAG_SNAPSHOT);
    }
    if (work->preserve_buf != NULL) {
        RtlSecureZeroMemory(work->preserve_buf, work->preserve_len);
        ExFreePoolWithTag(work->preserve_buf, SAR_POOL_TAG_PRESBUF);
    }

    ObDereferenceObject(work->originator_thread);
    ObDereferenceObject(work->originator_process);
    FltFreeGenericWorkItem(FltWorkItem);
    ExFreeToLookasideListEx(&ctx->work_pool, work);

    InterlockedDecrement(&ctx->inflight);
    ExReleaseRundownProtection(&ctx->rundown);
}

_IRQL_requires_max_(APC_LEVEL)
static BOOLEAN SarCaptureProvenanceIsOwn(_In_ const UINT16 *Path)
{
    static const UINT16 marker[] = { 'S', 'e', 'm', 'a', 'n', 't', 'i', 'c', 's', 'A', 'r' };
    ULONG i, j;

    for (i = 0; i < SEMANTICS_AR_PROVENANCE_PATH_MAX; i++) {
        if (Path[i] == 0)
            break;
        for (j = 0; j < RTL_NUMBER_OF(marker); j++) {
            if (i + j >= SEMANTICS_AR_PROVENANCE_PATH_MAX || Path[i + j] != marker[j])
                break;
        }
        if (j == RTL_NUMBER_OF(marker))
            return TRUE;
    }
    return FALSE;
}

_IRQL_requires_max_(APC_LEVEL)
static VOID SarSamplerNote(_In_ PSAR_CAPTURE_CTX Ctx, _In_ PSAR_WRITE_SEAM_REQUEST Request)
{
    LARGE_INTEGER now;
    PSAR_FLAG_SLOT slot = NULL;
    ULONG i;
    BOOLEAN found = FALSE;
    BOOLEAN do_fill = FALSE;
    BOOLEAN kick = FALSE;

    KeQuerySystemTime(&now);

    FltAcquirePushLockShared(&Ctx->flag_lock);
    for (i = 0; i < Ctx->flag_count; i++) {
        if (Ctx->flags[i].process == Request->originator_process) {
            slot = &Ctx->flags[i];
            InterlockedAdd64(&slot->progress_bytes, (LONG64)Request->write_length);
            slot->last_write_time = now;
            if (now.QuadPart - slot->last_pc_refresh.QuadPart >= SAR_SAMPLER_PCREFRESH_100NS)
                do_fill = TRUE;
            if (slot->progress_bytes >= (LONG64)SAR_SAMPLER_CADENCE_BYTES)
                kick = TRUE;
            found = TRUE;
            break;
        }
    }
    FltReleasePushLock(&Ctx->flag_lock);

    if (!found) {
        FltAcquirePushLockExclusive(&Ctx->flag_lock);
        for (i = 0; i < Ctx->flag_count; i++) {
            if (Ctx->flags[i].process == Request->originator_process) {
                slot = &Ctx->flags[i];
                InterlockedAdd64(&slot->progress_bytes, (LONG64)Request->write_length);
                slot->last_write_time = now;
                found = TRUE;
                do_fill = TRUE;
                break;
            }
        }
        if (!found && Ctx->flag_count < SAR_SAMPLER_CAPACITY) {
            slot = &Ctx->flags[Ctx->flag_count];
            RtlZeroMemory(slot, sizeof(*slot));
            ObReferenceObject(Request->originator_process);
            slot->process = Request->originator_process;
            slot->progress_bytes = (LONG64)Request->write_length;
            slot->last_write_time = now;
            slot->last_sample_time = now;
            Ctx->flag_count++;
            found = TRUE;
            do_fill = TRUE;
        }
        FltReleasePushLock(&Ctx->flag_lock);
    }

    if (found && do_fill && KeGetCurrentIrql() == PASSIVE_LEVEL) {
        UINT16 prov[SEMANTICS_AR_PROVENANCE_PATH_MAX];
        UCHAR wr[SAR_CAPTURE_BUFFER_BYTES];
        ULONG wr_len;
        ULONG pre_len;

        SarCaptureResolveProvenance(Request->data, prov);
        wr_len = SarCaptureCopyWritten(Request->data, wr, SAR_CAPTURE_BUFFER_BYTES);
        pre_len = Request->pre_image_len < SAR_CAPTURE_BUFFER_BYTES ?
                  Request->pre_image_len : SAR_CAPTURE_BUFFER_BYTES;

        FltAcquirePushLockExclusive(&Ctx->flag_lock);
        slot = NULL;
        for (i = 0; i < Ctx->flag_count; i++) {
            if (Ctx->flags[i].process == Request->originator_process) {
                slot = &Ctx->flags[i];
                break;
            }
        }
        if (slot != NULL) {
            RtlCopyMemory(slot->written, wr, wr_len);
            slot->written_len = wr_len;
            RtlCopyMemory(slot->pre_image, Request->pre_image, pre_len);
            slot->pre_image_len = pre_len;
            slot->file_offset = (UINT64)Request->write_offset.QuadPart;
            slot->provenance_offset = slot->file_offset;
            slot->write_length = Request->write_length;
            RtlCopyMemory(slot->provenance_path, prov, sizeof(prov));
            slot->last_pc_refresh = now;
        }
        FltReleasePushLock(&Ctx->flag_lock);
    }

    if (kick)
        KeSetEvent(&Ctx->sampler_wake, IO_NO_INCREMENT, FALSE);
}

_IRQL_requires_(PASSIVE_LEVEL)
static VOID SarSamplerSampleOne(_In_ PSAR_CAPTURE_CTX Ctx, _In_ PEPROCESS Process,
                                _In_ PSAR_FLAG_SLOT Md)
{
    HANDLE hproc;
    ULONG scan_len = 0;

    if (!NT_SUCCESS(ObOpenObjectByPointer(Process, OBJ_KERNEL_HANDLE, NULL, 0, *PsProcessType,
                                          KernelMode, &hproc)))
        return;

    if (NT_SUCCESS(PsAcquireProcessExitSynchronization(Process))) {
        KAPC_STATE apc;
        SAR_PROCESS_BASIC_INFORMATION pbi;
        ULONG pret = 0;

        KeStackAttachProcess(Process, &apc);
        if (NT_SUCCESS(ZwQueryInformationProcess(hproc, ProcessBasicInformation, &pbi,
                                                 sizeof(pbi), &pret)) &&
            pbi.PebBaseAddress != NULL)
            scan_len = SarCaptureSnapshotProcess(hproc, pbi.PebBaseAddress, Ctx->sampler_buf,
                                                 SAR_CAPTURE_HEAP_BUDGET);
        KeUnstackDetachProcess(&apc);
        PsReleaseProcessExitSynchronization(Process);
    }
    ZwClose(hproc);

    if (scan_len > 0) {
        sar_capture_request_t req;

        req.plaintext = Md->pre_image;
        req.ciphertext = Md->written;
        req.sample_size = Md->pre_image_len < Md->written_len ? Md->pre_image_len : Md->written_len;
        req.file_offset = Md->file_offset;
        req.candidates = NULL;
        req.candidate_count = 0;
        req.scan_buffer = Ctx->sampler_buf;
        req.scan_length = scan_len;
        req.provenance_path = Md->provenance_path;
        req.provenance_offset = Md->provenance_offset;
        req.provenance_length = Md->write_length;

        if (sar_capture_run(&req, Ctx->sampler_map, SarKeystoreMacKey(g_sar.keystore),
                            Ctx->sampler_result) == SAR_CAPTURE_CONVICTED)
            SarCaptureCommit(Ctx, Ctx->sampler_result, Process);

        RtlSecureZeroMemory(Ctx->sampler_result, sizeof(*Ctx->sampler_result));
        RtlSecureZeroMemory(Ctx->sampler_buf, scan_len);
    }
}

_IRQL_requires_(PASSIVE_LEVEL)
static VOID SarSamplerSweep(_In_ PSAR_CAPTURE_CTX Ctx)
{
    for (;;) {
        LARGE_INTEGER now;
        SAR_FLAG_SLOT pick;
        PEPROCESS proc = NULL;
        ULONG i;

        KeQuerySystemTime(&now);

        FltAcquirePushLockExclusive(&Ctx->flag_lock);

        i = 0;
        while (i < Ctx->flag_count) {
            if (now.QuadPart - Ctx->flags[i].last_write_time.QuadPart >= SAR_SAMPLER_QUIESCE_100NS) {
                ObDereferenceObject(Ctx->flags[i].process);
                Ctx->flags[i] = Ctx->flags[Ctx->flag_count - 1];
                Ctx->flag_count--;
                continue;
            }
            i++;
        }

        for (i = 0; i < Ctx->flag_count; i++) {
            PSAR_FLAG_SLOT s = &Ctx->flags[i];
            BOOLEAN due = (s->progress_bytes >= (LONG64)SAR_SAMPLER_CADENCE_BYTES) ||
                          (now.QuadPart - s->last_sample_time.QuadPart >= SAR_SAMPLER_REVISIT_100NS);

            if (!due)
                continue;

            s->last_sample_time = now;
            s->progress_bytes = 0;

            if (s->pre_image_len >= SAR_CANDIDATE_SIZE && s->written_len >= SAR_CANDIDATE_SIZE) {
                ObReferenceObject(s->process);
                proc = s->process;
                pick = *s;
                break;
            }
        }

        FltReleasePushLock(&Ctx->flag_lock);

        if (proc == NULL)
            break;

        SarSamplerSampleOne(Ctx, proc, &pick);
        ObDereferenceObject(proc);
    }
}

_Function_class_(KSTART_ROUTINE)
_IRQL_requires_(PASSIVE_LEVEL)
static VOID SarSamplerThread(_In_ PVOID Context)
{
    PSAR_CAPTURE_CTX ctx = (PSAR_CAPTURE_CTX)Context;
    LARGE_INTEGER wait;

    wait.QuadPart = SAR_SAMPLER_WAIT_100NS;

    for (;;) {
        KeWaitForSingleObject(&ctx->sampler_wake, Executive, KernelMode, FALSE, &wait);
        if (ctx->sampler_stop != 0)
            break;
        SarSamplerSweep(ctx);
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}

_IRQL_requires_(PASSIVE_LEVEL)
static NTSTATUS SarSamplerStart(_In_ PSAR_CAPTURE_CTX Ctx)
{
    OBJECT_ATTRIBUTES oa;
    HANDLE th;
    NTSTATUS status;

    KeInitializeEvent(&Ctx->sampler_wake, SynchronizationEvent, FALSE);
    Ctx->sampler_stop = 0;
    Ctx->sampler_thread = NULL;

    InitializeObjectAttributes(&oa, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    status = PsCreateSystemThread(&th, THREAD_ALL_ACCESS, &oa, NULL, NULL, SarSamplerThread, Ctx);
    if (!NT_SUCCESS(status))
        return status;

    status = ObReferenceObjectByHandle(th, SYNCHRONIZE, *PsThreadType, KernelMode,
                                       &Ctx->sampler_thread, NULL);
    if (!NT_SUCCESS(status)) {
        Ctx->sampler_stop = 1;
        KeSetEvent(&Ctx->sampler_wake, IO_NO_INCREMENT, FALSE);
        ZwWaitForSingleObject(th, FALSE, NULL);
        Ctx->sampler_thread = NULL;
    }
    ZwClose(th);
    return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
static VOID SarSamplerStop(_In_ PSAR_CAPTURE_CTX Ctx)
{
    if (Ctx->sampler_thread == NULL)
        return;

    Ctx->sampler_stop = 1;
    KeSetEvent(&Ctx->sampler_wake, IO_NO_INCREMENT, FALSE);
    KeWaitForSingleObject(Ctx->sampler_thread, Executive, KernelMode, FALSE, NULL);
    ObDereferenceObject(Ctx->sampler_thread);
    Ctx->sampler_thread = NULL;
}

_IRQL_requires_max_(APC_LEVEL)
VOID SarCaptureSubmitWrite(_In_opt_ PSAR_CAPTURE_CTX Ctx, _Inout_ PSAR_WRITE_SEAM_REQUEST Request)
{
    PSAR_CAPTURE_WORK work;
    PFLT_GENERIC_WORKITEM item;
    HANDLE pid;

    SarSeamWriteSubmit(Request);

    if (Ctx == NULL || !SarCaptureMemberCapturable(Request->member)) {
        SarSeamWriteRelease(Request);
        return;
    }

    if (!SarKeystoreReady(g_sar.keystore)) {
        InterlockedIncrement64(&Ctx->preload_drops);
        SarSeamWriteRelease(Request);
        return;
    }

    pid = PsGetProcessId(Request->originator_process);
    if (SarStateIdentityLookup(g_sar_state, pid) == SAR_IDSTATE_EXEMPT) {
        InterlockedIncrement64(&Ctx->whitelist_skips);
        SarSeamWriteRelease(Request);
        return;
    }

    if (Request->pre_image_len < SAR_CANDIDATE_SIZE) {
        SarSeamWriteRelease(Request);
        return;
    }

    SarSamplerNote(Ctx, Request);

    if (InterlockedIncrement(&Ctx->inflight) > SAR_CAPTURE_INFLIGHT_CAP) {
        InterlockedDecrement(&Ctx->inflight);
        InterlockedIncrement64(&Ctx->pressure_drops);
        SarSeamWriteRelease(Request);
        return;
    }

    if (!ExAcquireRundownProtection(&Ctx->rundown)) {
        InterlockedDecrement(&Ctx->inflight);
        SarSeamWriteRelease(Request);
        return;
    }

    work = (PSAR_CAPTURE_WORK)ExAllocateFromLookasideListEx(&Ctx->work_pool);
    if (work == NULL) {
        ExReleaseRundownProtection(&Ctx->rundown);
        InterlockedDecrement(&Ctx->inflight);
        SarSeamWriteRelease(Request);
        return;
    }

    item = FltAllocateGenericWorkItem();
    if (item == NULL) {
        ExFreeToLookasideListEx(&Ctx->work_pool, work);
        ExReleaseRundownProtection(&Ctx->rundown);
        InterlockedDecrement(&Ctx->inflight);
        SarSeamWriteRelease(Request);
        return;
    }

    work->ctx = Ctx;
    work->work_item = item;
    work->originator_process = Request->originator_process;
    work->originator_thread = Request->originator_thread;
    work->file_offset = (UINT64)Request->write_offset.QuadPart;
    work->write_length = Request->write_length;
    work->provenance_offset = work->file_offset;

    SarCaptureResolveProvenance(Request->data, work->provenance_path);
    if (SarCaptureProvenanceIsOwn(work->provenance_path)) {
        FltFreeGenericWorkItem(item);
        ExFreeToLookasideListEx(&Ctx->work_pool, work);
        ExReleaseRundownProtection(&Ctx->rundown);
        InterlockedDecrement(&Ctx->inflight);
        SarSeamWriteRelease(Request);
        return;
    }
    work->written_len = SarCaptureCopyWritten(Request->data, work->written, SAR_CAPTURE_BUFFER_BYTES);
    work->pre_image_len = Request->pre_image_len < SAR_CAPTURE_BUFFER_BYTES ?
                          Request->pre_image_len : SAR_CAPTURE_BUFFER_BYTES;
    RtlCopyMemory(work->pre_image, Request->pre_image, work->pre_image_len);

    work->scan = NULL;
    work->scan_len = 0;
    work->preserve_buf = NULL;
    work->preserve_len = 0;

    if (Request->related != NULL && !FlagOn(Request->irp_flags, IRP_NOCACHE) &&
        g_sar.preserve != NULL && SarPreserveReady(g_sar.preserve) &&
        Request->write_length > 0 && Request->write_length <= SAR_PRESERVE_STAGE_MAX) {
        PUCHAR pbuf = (PUCHAR)ExAllocatePool2(POOL_FLAG_PAGED,
                                               Request->write_length, SAR_POOL_TAG_PRESBUF);
        if (pbuf != NULL) {
            ULONG got = 0;
            if (NT_SUCCESS(FltReadFile(Request->related->Instance, Request->related->FileObject,
                                        &Request->write_offset, Request->write_length, pbuf,
                                        0, &got, NULL, NULL)) &&
                got >= Request->write_length) {
                work->preserve_buf = pbuf;
                work->preserve_len = got;
            } else {
                ExFreePoolWithTag(pbuf, SAR_POOL_TAG_PRESBUF);
            }
        }
    }

    Request->originator_process = NULL;
    Request->originator_thread = NULL;

    if (!NT_SUCCESS(FltQueueGenericWorkItem(item, Ctx->filter, SarCaptureWorker,
                                            DelayedWorkQueue, work))) {
        ObDereferenceObject(work->originator_thread);
        ObDereferenceObject(work->originator_process);
        if (work->preserve_buf != NULL) {
            RtlSecureZeroMemory(work->preserve_buf, work->preserve_len);
            ExFreePoolWithTag(work->preserve_buf, SAR_POOL_TAG_PRESBUF);
        }
        FltFreeGenericWorkItem(item);
        ExFreeToLookasideListEx(&Ctx->work_pool, work);
        ExReleaseRundownProtection(&Ctx->rundown);
        InterlockedDecrement(&Ctx->inflight);
    }
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarCaptureCreate(_In_ PFLT_FILTER Filter, _Outptr_ PSAR_CAPTURE_CTX *Ctx)
{
    PSAR_CAPTURE_CTX ctx;
    NTSTATUS status;

    *Ctx = NULL;

    ctx = (PSAR_CAPTURE_CTX)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*ctx), SAR_POOL_TAG_CAPCTX);
    if (ctx == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    ctx->filter = Filter;
    ctx->blocked_count = 0;
    ctx->inflight = 0;
    ctx->whitelist_skips = 0;
    ctx->pressure_drops = 0;
    ctx->preload_drops = 0;
    ctx->work_pool_init = FALSE;

    ctx->blocked = (PEPROCESS *)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                                SAR_BLOCKED_CAPACITY * sizeof(PEPROCESS),
                                                SAR_POOL_TAG_BLOCKED);
    if (ctx->blocked == NULL) {
        ExFreePoolWithTag(ctx, SAR_POOL_TAG_CAPCTX);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = ExInitializeLookasideListEx(&ctx->work_pool, NULL, NULL, NonPagedPoolNx, 0,
                                         sizeof(SAR_CAPTURE_WORK), SAR_POOL_TAG_CAPWORK, 0);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(ctx->blocked, SAR_POOL_TAG_BLOCKED);
        ExFreePoolWithTag(ctx, SAR_POOL_TAG_CAPCTX);
        return status;
    }
    ctx->work_pool_init = TRUE;

    FltInitializePushLock(&ctx->blocked_lock);
    FltInitializePushLock(&ctx->flag_lock);
    ExInitializeRundownProtection(&ctx->rundown);

    ctx->flag_count = 0;
    ctx->sampler_thread = NULL;
    ctx->flags = (PSAR_FLAG_SLOT)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                                 SAR_SAMPLER_CAPACITY * sizeof(SAR_FLAG_SLOT),
                                                 SAR_POOL_TAG_FLAGS);
    ctx->sampler_buf = (PUCHAR)ExAllocatePool2(POOL_FLAG_PAGED, SAR_CAPTURE_HEAP_BUDGET,
                                               SAR_POOL_TAG_SNAPSHOT);
    ctx->sampler_map = (sar_gate_map_t *)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(sar_gate_map_t),
                                                         SAR_POOL_TAG_GATEMAP);
    ctx->sampler_result = (sar_capture_result_t *)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                                                  sizeof(sar_capture_result_t),
                                                                  SAR_POOL_TAG_CAPRES);
    if (ctx->flags == NULL || ctx->sampler_buf == NULL || ctx->sampler_map == NULL ||
        ctx->sampler_result == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto fail;
    }

    status = SarSamplerStart(ctx);
    if (!NT_SUCCESS(status))
        goto fail;

    *Ctx = ctx;
    return STATUS_SUCCESS;

fail:
    if (ctx->sampler_result != NULL)
        ExFreePoolWithTag(ctx->sampler_result, SAR_POOL_TAG_CAPRES);
    if (ctx->sampler_map != NULL)
        ExFreePoolWithTag(ctx->sampler_map, SAR_POOL_TAG_GATEMAP);
    if (ctx->sampler_buf != NULL)
        ExFreePoolWithTag(ctx->sampler_buf, SAR_POOL_TAG_SNAPSHOT);
    if (ctx->flags != NULL)
        ExFreePoolWithTag(ctx->flags, SAR_POOL_TAG_FLAGS);
    FltDeletePushLock(&ctx->flag_lock);
    FltDeletePushLock(&ctx->blocked_lock);
    ExDeleteLookasideListEx(&ctx->work_pool);
    ExFreePoolWithTag(ctx->blocked, SAR_POOL_TAG_BLOCKED);
    ExFreePoolWithTag(ctx, SAR_POOL_TAG_CAPCTX);
    return status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarCaptureDestroy(_Inout_ PSAR_CAPTURE_CTX Ctx)
{
    ULONG i;

    if (Ctx == NULL)
        return;

    SarSamplerStop(Ctx);

    ExWaitForRundownProtectionRelease(&Ctx->rundown);

    for (i = 0; i < Ctx->blocked_count; i++)
        ObDereferenceObject(Ctx->blocked[i]);

    for (i = 0; i < Ctx->flag_count; i++)
        ObDereferenceObject(Ctx->flags[i].process);

    if (Ctx->work_pool_init)
        ExDeleteLookasideListEx(&Ctx->work_pool);

    FltDeletePushLock(&Ctx->blocked_lock);
    FltDeletePushLock(&Ctx->flag_lock);

    if (Ctx->blocked != NULL)
        ExFreePoolWithTag(Ctx->blocked, SAR_POOL_TAG_BLOCKED);
    if (Ctx->flags != NULL)
        ExFreePoolWithTag(Ctx->flags, SAR_POOL_TAG_FLAGS);
    if (Ctx->sampler_buf != NULL)
        ExFreePoolWithTag(Ctx->sampler_buf, SAR_POOL_TAG_SNAPSHOT);
    if (Ctx->sampler_map != NULL)
        ExFreePoolWithTag(Ctx->sampler_map, SAR_POOL_TAG_GATEMAP);
    if (Ctx->sampler_result != NULL)
        ExFreePoolWithTag(Ctx->sampler_result, SAR_POOL_TAG_CAPRES);

    ExFreePoolWithTag(Ctx, SAR_POOL_TAG_CAPCTX);
}
