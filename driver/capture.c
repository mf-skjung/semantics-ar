#include "capture.h"
#include "state.h"
#include "commport.h"
#include "keystore_persist.h"
#include "ntsystem.h"

#include <ntifs.h>

#include "sar_capture.h"
#include "aes.h"

extern PSAR_STATE g_sar_state;

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
static ULONG SarCaptureSnapshotHeaps(_In_ HANDLE ProcessHandle, _In_ PVOID Peb,
                                     _Out_writes_bytes_(Cap) PUCHAR Buf, _In_ ULONG Cap)
{
    ULONG total = 0;

    __try {
        ULONG num = *(ULONG *)((PUCHAR)Peb + SAR_PEB_NUMBEROFHEAPS);
        PVOID *heaps = *(PVOID **)((PUCHAR)Peb + SAR_PEB_PROCESSHEAPS);
        ULONG i;

        if (heaps == NULL)
            return 0;
        if (num > SAR_CAPTURE_HEAP_MAX)
            num = SAR_CAPTURE_HEAP_MAX;

        for (i = 0; i < num && total < Cap; i++) {
            ULONG_PTR base = (ULONG_PTR)heaps[i];
            ULONG remain = Cap - total;
            ULONG perheap = remain < SAR_CAPTURE_HEAP_PER_CAP ? remain : SAR_CAPTURE_HEAP_PER_CAP;

            if (base == 0)
                continue;
            total += SarCaptureSnapshotHeap(ProcessHandle, base, Buf + total, perheap);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        /* PEB layout mismatch or unreadable: degrade to a miss, never crash */
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
    if (SarKeystoreAppend(g_sar.keystore, &Result->record) != 1)
        return;

    SarCaptureSendNotify(Ctx, &Result->notify);

    if (SarStateModeGet(g_sar_state) == SEMANTICS_AR_MODE_ENFORCE)
        SarCaptureBlockOriginator(Ctx, Originator);
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
                        work->scan_len = SarCaptureSnapshotHeaps(hproc, pbi.PebBaseAddress,
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

        if (sar_capture_run(&req, map, SarKeystoreMacKey(g_sar.keystore), result) ==
            SAR_CAPTURE_CONVICTED) {
            SarCaptureCommit(ctx, result, work->originator_process);
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

    Request->originator_process = NULL;
    Request->originator_thread = NULL;

    if (!NT_SUCCESS(FltQueueGenericWorkItem(item, Ctx->filter, SarCaptureWorker,
                                            DelayedWorkQueue, work))) {
        ObDereferenceObject(work->originator_thread);
        ObDereferenceObject(work->originator_process);
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
    ExInitializeRundownProtection(&ctx->rundown);

    *Ctx = ctx;
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarCaptureDestroy(_Inout_ PSAR_CAPTURE_CTX Ctx)
{
    ULONG i;

    if (Ctx == NULL)
        return;

    ExWaitForRundownProtectionRelease(&Ctx->rundown);

    for (i = 0; i < Ctx->blocked_count; i++)
        ObDereferenceObject(Ctx->blocked[i]);

    if (Ctx->work_pool_init)
        ExDeleteLookasideListEx(&Ctx->work_pool);

    FltDeletePushLock(&Ctx->blocked_lock);

    if (Ctx->blocked != NULL)
        ExFreePoolWithTag(Ctx->blocked, SAR_POOL_TAG_BLOCKED);

    ExFreePoolWithTag(Ctx, SAR_POOL_TAG_CAPCTX);
}
