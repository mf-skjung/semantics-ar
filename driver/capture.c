#include "capture.h"
#include "state.h"
#include "commport.h"
#include "keystore_persist.h"
#include "preserve.h"
#include "store_io.h"
#include "eventlog.h"
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

    EX_PUSH_LOCK scanned_lock;
    PEPROCESS *scanned;
    ULONG scanned_count;
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
    BOOLEAN preserve_only;
    BOOLEAN read_by_path;
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

_IRQL_requires_max_(APC_LEVEL)
VOID SarCaptureBlockOriginator(_In_ PSAR_CAPTURE_CTX Ctx, _In_ PEPROCESS Originator,
                               _In_ UINT32 EventClass)
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

    SarEventLogRecord(g_sar.eventlog, EventClass, PsGetProcessStartKey(Originator));
}

_IRQL_requires_max_(APC_LEVEL)
VOID SarCaptureNoteCapacityRefusal(_In_ PEPROCESS Originator)
{
    SarEventLogRecord(g_sar.eventlog, SAR_EVENT_CLASS_BLOCK_CAPACITY,
                      PsGetProcessStartKey(Originator));
}

_IRQL_requires_max_(APC_LEVEL)
LONG SarCaptureInflight(_In_opt_ PSAR_CAPTURE_CTX Ctx)
{
    if (Ctx == NULL)
        return 0;
    return InterlockedCompareExchange(&Ctx->inflight, 0, 0);
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
    int appended = SarKeystoreAppend(g_sar.keystore, &Result->record);
    if (appended == 1) {
        SarCaptureSendNotify(Ctx, &Result->notify);
        SarEventLogRecord(g_sar.eventlog, SAR_EVENT_CLASS_KEY_CAPTURED,
                          Result->record.actor_start_key);
    }

    if (appended == 1 || appended == 0)
        SarPreserveReconcile(g_sar.preserve, Result->record.provenance_path,
                             Result->record.provenance_offset, Result->record.provenance_length);

    SarPreservePromote(g_sar.preserve, (UINT64)(ULONG_PTR)PsGetProcessId(Originator));

    if (SarStateModeGet(g_sar_state) == SEMANTICS_AR_MODE_ENFORCE)
        SarCaptureBlockOriginator(Ctx, Originator, SAR_EVENT_CLASS_BLOCK_FORWARD);
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
        SarCaptureNoteCapacityRefusal(Work->originator_process);
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
        UINT64 actor = (UINT64)(ULONG_PTR)PsGetProcessId(Work->originator_process);
        SarPreserveStage(g_sar.preserve, Work->provenance_path, Work->file_offset,
                         Work->write_length, actor, Work->preserve_buf, Work->write_length, FALSE);
    }
}

_IRQL_requires_(PASSIVE_LEVEL)
static VOID SarCapturePreserveDestruction(_In_ PSAR_CAPTURE_WORK Work)
{
    SIZE_T pathchars;
    UINT64 actor;
    PUCHAR localbuf = NULL;
    const UCHAR *bytes;
    ULONG len;

    if (g_sar.preserve == NULL || !SarPreserveReady(g_sar.preserve))
        return;

    for (pathchars = 0; pathchars < SEMANTICS_AR_PROVENANCE_PATH_MAX; pathchars++) {
        if (Work->provenance_path[pathchars] == 0)
            break;
    }
    if (pathchars == 0)
        return;

    if (Work->read_by_path) {
        UNICODE_STRING name;
        OBJECT_ATTRIBUTES oa;
        IO_STATUS_BLOCK iosb;
        HANDLE h = NULL;
        LARGE_INTEGER off;

        localbuf = (PUCHAR)ExAllocatePool2(POOL_FLAG_PAGED, SAR_PRESERVE_STAGE_MAX,
                                           SAR_POOL_TAG_PRESBUF);
        if (localbuf == NULL)
            return;
        name.Buffer = (PWCH)Work->provenance_path;
        name.Length = (USHORT)(pathchars * sizeof(WCHAR));
        name.MaximumLength = name.Length;
        InitializeObjectAttributes(&oa, &name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
        off.QuadPart = 0;
        if (!NT_SUCCESS(ZwCreateFile(&h, FILE_READ_DATA | SYNCHRONIZE, &oa, &iosb, NULL,
                                     FILE_ATTRIBUTE_NORMAL,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     FILE_OPEN,
                                     FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
                                     NULL, 0))) {
            ExFreePoolWithTag(localbuf, SAR_POOL_TAG_PRESBUF);
            return;
        }
        if (!NT_SUCCESS(ZwReadFile(h, NULL, NULL, NULL, &iosb, localbuf, SAR_PRESERVE_STAGE_MAX,
                                   &off, NULL)) ||
            iosb.Information == 0) {
            ZwClose(h);
            ExFreePoolWithTag(localbuf, SAR_POOL_TAG_PRESBUF);
            return;
        }
        ZwClose(h);
        bytes = localbuf;
        len = (ULONG)iosb.Information;
    } else {
        if (Work->preserve_buf == NULL || Work->preserve_len == 0)
            return;
        bytes = Work->preserve_buf;
        len = Work->preserve_len;
    }

    actor = (UINT64)(ULONG_PTR)PsGetProcessId(Work->originator_process);
    SarPreserveStage(g_sar.preserve, Work->provenance_path, Work->provenance_offset,
                     len, actor, bytes, len, FALSE);

    if (localbuf != NULL) {
        RtlSecureZeroMemory(localbuf, SAR_PRESERVE_STAGE_MAX);
        ExFreePoolWithTag(localbuf, SAR_POOL_TAG_PRESBUF);
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

    if (work->preserve_only) {
        SarCapturePreserveDestruction(work);
        map = NULL;
        result = NULL;
        goto cleanup;
    }

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
            BOOLEAN already_scanned = FALSE;
            ULONG si;

            FltAcquirePushLockShared(&ctx->scanned_lock);
            for (si = 0; si < ctx->scanned_count; si++) {
                if (ctx->scanned[si] == work->originator_process) {
                    already_scanned = TRUE;
                    break;
                }
            }
            FltReleasePushLock(&ctx->scanned_lock);

            if (!already_scanned) {
                work->scan = (PUCHAR)ExAllocatePool2(POOL_FLAG_PAGED, SAR_CAPTURE_HEAP_BUDGET,
                                                     SAR_POOL_TAG_SNAPSHOT);
            }
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

            FltAcquirePushLockExclusive(&ctx->scanned_lock);
            {
                BOOLEAN found = FALSE;
                for (ULONG si = 0; si < ctx->scanned_count; si++) {
                    if (ctx->scanned[si] == work->originator_process) {
                        found = TRUE;
                        break;
                    }
                }
                if (!found && ctx->scanned_count < SAR_SCANNED_CAPACITY) {
                    ObReferenceObject(work->originator_process);
                    ctx->scanned[ctx->scanned_count] = work->originator_process;
                    ctx->scanned_count++;
                }
            }
            FltReleasePushLock(&ctx->scanned_lock);
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
        {
            LARGE_INTEGER now;
            KeQuerySystemTime(&now);
            req.capture_time = (UINT64)now.QuadPart;
        }
        req.actor_start_key = PsGetProcessStartKey(work->originator_process);

        if (sar_capture_run(&req, map, SarKeystoreMacKey(g_sar.keystore), result) ==
            SAR_CAPTURE_CONVICTED) {
            SarCaptureCommit(ctx, result, work->originator_process);
        } else if (gate.candidate) {
            SarCapturePreserveFromWork(ctx, work);
        }
    }

cleanup:
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
static BOOLEAN SarCapturePathExempt(_In_ const UINT16 *Path)
{
    UNICODE_STRING name;
    SIZE_T chars;

    if (Path[0] == 0 || SarCaptureProvenanceIsOwn(Path))
        return TRUE;

    for (chars = 0; chars < SEMANTICS_AR_PROVENANCE_PATH_MAX && Path[chars] != 0; chars++) { }
    name.Buffer = (PWCH)Path;
    name.Length = (USHORT)(chars * sizeof(WCHAR));
    name.MaximumLength = name.Length;
    return SarTargetExempt(&name);
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

    if (Request->provenance_override != NULL) {
        ULONG pi;
        for (pi = 0; pi < SEMANTICS_AR_PROVENANCE_PATH_MAX - 1 &&
                     Request->provenance_override[pi] != 0; pi++)
            work->provenance_path[pi] = Request->provenance_override[pi];
        work->provenance_path[pi] = 0;
    } else {
        SarCaptureResolveProvenance(Request->data, work->provenance_path);
    }
    if (SarCapturePathExempt(work->provenance_path)) {
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
    work->preserve_only = FALSE;
    work->read_by_path = FALSE;

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
static BOOLEAN SarCaptureStreamConfidentBlind(_In_ PFLT_INSTANCE Instance,
                                              _In_ PFILE_OBJECT FileObject)
{
    PSAR_STREAM_CONTEXT sc = NULL;
    BOOLEAN blind = FALSE;

    if (NT_SUCCESS(FltGetStreamContext(Instance, FileObject, (PFLT_CONTEXT *)&sc))) {
        LONG f = sc->flags;
        blind = (f & SAR_STREAMCTX_FLAG_TRACKED_FROM_OPEN) != 0 &&
                (f & SAR_STREAMCTX_FLAG_READ_OBSERVED) == 0 &&
                (f & SAR_STREAMCTX_FLAG_SECTION_DIRTY) == 0;
        FltReleaseContext(sc);
    }
    return blind;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static SAR_STAGE_RESULT SarCaptureWholeContent(_In_ PSAR_CAPTURE_CTX Ctx, _In_ PFLT_INSTANCE Instance,
                                               _In_ PFILE_OBJECT FileObject, _In_ const UINT16 *Path,
                                               _In_ PEPROCESS Originator);

_IRQL_requires_max_(PASSIVE_LEVEL)
static SAR_STAGE_RESULT SarCaptureLinkPreserve(_In_ PSAR_CAPTURE_CTX Ctx, _In_ PFLT_INSTANCE Instance,
                                   _In_ PFILE_OBJECT FileObject, _In_ const UINT16 *Path,
                                   _In_ PEPROCESS Originator, _In_ PETHREAD Thread)
{
    FILE_STANDARD_INFORMATION fsi;
    UINT64 size;
    UINT64 linkid;
    UINT64 actor;
    HANDLE pid;
    WCHAR dirbuf[96];
    WCHAR linkbuf[128];
    UCHAR linkInfoBuf[sizeof(FILE_LINK_INFORMATION) + sizeof(linkbuf)];
    PFILE_LINK_INFORMATION li = (PFILE_LINK_INFORMATION)linkInfoBuf;
    UNICODE_STRING linkName;
    IO_STATUS_BLOCK iosb;
    NTSTATUS status;

    if (Ctx == NULL || g_sar.preserve == NULL || !SarPreserveReady(g_sar.preserve))
        return SAR_STAGE_STORED;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return SAR_STAGE_STORED;
    if (SarCapturePathExempt(Path))
        return SAR_STAGE_STORED;
    pid = PsGetProcessId(Originator);
    if (SarStateIdentityLookup(g_sar_state, pid) == SAR_IDSTATE_EXEMPT)
        return SAR_STAGE_STORED;
    if (SarCaptureStreamConfidentBlind(Instance, FileObject))
        return SAR_STAGE_STORED;

    RtlZeroMemory(&fsi, sizeof(fsi));
    if (!NT_SUCCESS(FltQueryInformationFile(Instance, FileObject, &fsi, sizeof(fsi),
                                            FileStandardInformation, NULL)))
        return SAR_STAGE_STORED;
    size = (UINT64)fsi.EndOfFile.QuadPart;
    if (size == 0)
        return SAR_STAGE_STORED;

    actor = (UINT64)(ULONG_PTR)pid;
    linkid = SarPreserveAllocLinkId(g_sar.preserve);
    if (!SarPreserveQuarantinePaths(Path, linkid, dirbuf, RTL_NUMBER_OF(dirbuf),
                                    linkbuf, RTL_NUMBER_OF(linkbuf)))
        return SAR_STAGE_FAILED;

    SarStoreEnsureDir(dirbuf, SAR_POOL_TAG_PRESBUF);

    RtlInitUnicodeString(&linkName, linkbuf);
    RtlZeroMemory(li, FIELD_OFFSET(FILE_LINK_INFORMATION, FileName));
    li->ReplaceIfExists = FALSE;
    li->RootDirectory = NULL;
    li->FileNameLength = linkName.Length;
    RtlCopyMemory(li->FileName, linkbuf, linkName.Length);

    status = FltSetInformationFile(Instance, FileObject, li,
                                   FIELD_OFFSET(FILE_LINK_INFORMATION, FileName) + linkName.Length,
                                   FileLinkInformation);
    if (NT_SUCCESS(status)) {
        if (SarPreserveStageLink(g_sar.preserve, Path, size, actor, linkid) != STATUS_SUCCESS) {
            OBJECT_ATTRIBUTES oa;
            InitializeObjectAttributes(&oa, &linkName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                                       NULL, NULL);
            ZwDeleteFile(&oa);
            UNREFERENCED_PARAMETER(iosb);
            return SAR_STAGE_DROPPED;
        }
        UNREFERENCED_PARAMETER(iosb);
        return SAR_STAGE_STORED;
    }

    UNREFERENCED_PARAMETER(iosb);
    UNREFERENCED_PARAMETER(Thread);
    return SarCaptureWholeContent(Ctx, Instance, FileObject, Path, Originator);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
SAR_STAGE_RESULT SarCaptureSubmitDelete(_In_opt_ PSAR_CAPTURE_CTX Ctx, _In_ PFLT_CALLBACK_DATA Data,
                            _In_ PCFLT_RELATED_OBJECTS FltObjects, _In_ PEPROCESS Originator,
                            _In_ PETHREAD Thread)
{
    UINT16 path[SEMANTICS_AR_PROVENANCE_PATH_MAX];

    if (Ctx == NULL || g_sar.preserve == NULL || !SarPreserveReady(g_sar.preserve))
        return SAR_STAGE_STORED;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return SAR_STAGE_STORED;

    SarCaptureResolveProvenance(Data, path);
    return SarCaptureLinkPreserve(Ctx, FltObjects->Instance, FltObjects->FileObject, path,
                                  Originator, Thread);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
SAR_STAGE_RESULT SarCaptureSubmitRenameTarget(_In_opt_ PSAR_CAPTURE_CTX Ctx, _In_ PFLT_CALLBACK_DATA Data,
                                  _In_ PCFLT_RELATED_OBJECTS FltObjects,
                                  _In_ PEPROCESS Originator, _In_ PETHREAD Thread)
{
    SAR_STAGE_RESULT result = SAR_STAGE_STORED;
    PVOID buf = Data->Iopb->Parameters.SetFileInformation.InfoBuffer;
    FILE_INFORMATION_CLASS cls = Data->Iopb->Parameters.SetFileInformation.FileInformationClass;
    HANDLE rootdir;
    PWSTR fname;
    ULONG fnamelen;
    BOOLEAN replace;
    PFLT_FILE_NAME_INFORMATION destName = NULL;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    HANDLE h = NULL;
    PFILE_OBJECT fo = NULL;
    UINT16 path[SEMANTICS_AR_PROVENANCE_PATH_MAX];
    ULONG i;
    ULONG chars;

    if (Ctx == NULL || buf == NULL || g_sar.preserve == NULL || !SarPreserveReady(g_sar.preserve))
        return SAR_STAGE_STORED;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return SAR_STAGE_STORED;

    {
        PFILE_RENAME_INFORMATION ri = (PFILE_RENAME_INFORMATION)buf;
        if (cls == FileRenameInformationEx || cls == FileLinkInformationEx)
            replace = (*(ULONG *)buf & FILE_RENAME_REPLACE_IF_EXISTS) != 0;
        else
            replace = ri->ReplaceIfExists;
        rootdir = ri->RootDirectory;
        fname = ri->FileName;
        fnamelen = ri->FileNameLength;
    }
    if (!replace || fnamelen == 0)
        return SAR_STAGE_STORED;

    if (!NT_SUCCESS(FltGetDestinationFileNameInformation(FltObjects->Instance,
                                                         FltObjects->FileObject, rootdir, fname,
                                                         fnamelen,
                                                         FLT_FILE_NAME_NORMALIZED |
                                                         FLT_FILE_NAME_QUERY_DEFAULT, &destName)))
        return SAR_STAGE_STORED;
    if (!NT_SUCCESS(FltParseFileNameInformation(destName))) {
        FltReleaseFileNameInformation(destName);
        return SAR_STAGE_STORED;
    }

    InitializeObjectAttributes(&oa, &destName->Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               NULL, NULL);
    if (!NT_SUCCESS(FltCreateFileEx(Ctx->filter, FltObjects->Instance, &h, &fo,
                                    FILE_READ_DATA | SYNCHRONIZE, &oa, &iosb, NULL,
                                    FILE_ATTRIBUTE_NORMAL,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    FILE_OPEN,
                                    FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
                                    NULL, 0, 0))) {
        FltReleaseFileNameInformation(destName);
        return SAR_STAGE_STORED;
    }

    RtlZeroMemory(path, sizeof(path));
    chars = destName->Name.Length / sizeof(WCHAR);
    if (chars > SEMANTICS_AR_PROVENANCE_PATH_MAX - 1)
        chars = SEMANTICS_AR_PROVENANCE_PATH_MAX - 1;
    for (i = 0; i < chars; i++)
        path[i] = (UINT16)destName->Name.Buffer[i];

    result = SarCaptureLinkPreserve(Ctx, FltObjects->Instance, fo, path, Originator, Thread);

    FltClose(h);
    ObDereferenceObject(fo);
    FltReleaseFileNameInformation(destName);
    return result;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarCaptureSubmitRenameRekey(_In_opt_ PSAR_CAPTURE_CTX Ctx, _In_ PFLT_CALLBACK_DATA Data,
                                 _In_ PCFLT_RELATED_OBJECTS FltObjects)
{
    PFILE_RENAME_INFORMATION ri = (PFILE_RENAME_INFORMATION)
        Data->Iopb->Parameters.SetFileInformation.InfoBuffer;
    PFLT_FILE_NAME_INFORMATION srcName = NULL;
    PFLT_FILE_NAME_INFORMATION destName = NULL;
    UINT16 oldp[SEMANTICS_AR_PROVENANCE_PATH_MAX];
    UINT16 newp[SEMANTICS_AR_PROVENANCE_PATH_MAX];
    ULONG i, chars;

    if (Ctx == NULL || ri == NULL || g_sar.preserve == NULL || !SarPreserveReady(g_sar.preserve))
        return;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || ri->FileNameLength == 0)
        return;

    if (!NT_SUCCESS(FltGetFileNameInformation(Data, FLT_FILE_NAME_NORMALIZED |
                                              FLT_FILE_NAME_QUERY_DEFAULT, &srcName)))
        return;
    if (!NT_SUCCESS(FltParseFileNameInformation(srcName))) {
        FltReleaseFileNameInformation(srcName);
        return;
    }
    if (!NT_SUCCESS(FltGetDestinationFileNameInformation(FltObjects->Instance,
                                                         FltObjects->FileObject, ri->RootDirectory,
                                                         ri->FileName, ri->FileNameLength,
                                                         FLT_FILE_NAME_NORMALIZED |
                                                         FLT_FILE_NAME_QUERY_DEFAULT, &destName)) ||
        !NT_SUCCESS(FltParseFileNameInformation(destName))) {
        if (destName != NULL)
            FltReleaseFileNameInformation(destName);
        FltReleaseFileNameInformation(srcName);
        return;
    }

    RtlZeroMemory(oldp, sizeof(oldp));
    RtlZeroMemory(newp, sizeof(newp));
    chars = srcName->Name.Length / sizeof(WCHAR);
    if (chars > SEMANTICS_AR_PROVENANCE_PATH_MAX - 1)
        chars = SEMANTICS_AR_PROVENANCE_PATH_MAX - 1;
    for (i = 0; i < chars; i++)
        oldp[i] = (UINT16)srcName->Name.Buffer[i];
    chars = destName->Name.Length / sizeof(WCHAR);
    if (chars > SEMANTICS_AR_PROVENANCE_PATH_MAX - 1)
        chars = SEMANTICS_AR_PROVENANCE_PATH_MAX - 1;
    for (i = 0; i < chars; i++)
        newp[i] = (UINT16)destName->Name.Buffer[i];

    SarPreserveRename(g_sar.preserve, oldp, newp);

    FltReleaseFileNameInformation(destName);
    FltReleaseFileNameInformation(srcName);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static SAR_STAGE_RESULT SarCaptureWholeContent(_In_ PSAR_CAPTURE_CTX Ctx, _In_ PFLT_INSTANCE Instance,
                                               _In_ PFILE_OBJECT FileObject, _In_ const UINT16 *Path,
                                               _In_ PEPROCESS Originator)
{
    PFLT_CONTEXT sectionCtx = NULL;
    HANDLE hSection = NULL;
    PVOID sectionObj = NULL;
    LARGE_INTEGER sectionFileSize;
    OBJECT_ATTRIBUTES oa;
    PVOID base = NULL;
    SIZE_T viewSize = 0;
    UINT64 size, offset;
    UINT64 actor;
    SAR_STAGE_RESULT worst = SAR_STAGE_STORED;

    if (SarCapturePathExempt(Path))
        return SAR_STAGE_STORED;

    if (!NT_SUCCESS(FltAllocateContext(Ctx->filter, FLT_SECTION_CONTEXT, sizeof(LONG),
                                       PagedPool, &sectionCtx)))
        return SAR_STAGE_STORED;
    InitializeObjectAttributes(&oa, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    sectionFileSize.QuadPart = 0;
    if (!NT_SUCCESS(FltCreateSectionForDataScan(Instance, FileObject, sectionCtx, FILE_READ_DATA,
                                                &oa, NULL, PAGE_READONLY, SEC_COMMIT, 0, &hSection,
                                                &sectionObj, &sectionFileSize))) {
        FltReleaseContext(sectionCtx);
        return SAR_STAGE_STORED;
    }

    size = (UINT64)sectionFileSize.QuadPart;
    if (size > 0 &&
        NT_SUCCESS(ZwMapViewOfSection(hSection, ZwCurrentProcess(), &base, 0, 0, NULL,
                                      &viewSize, ViewUnmap, 0, PAGE_READONLY))) {
        actor = (UINT64)(ULONG_PTR)PsGetProcessId(Originator);
        for (offset = 0; offset < size; offset += SAR_PRESERVE_STAGE_MAX) {
            UINT64 rem = size - offset;
            ULONG chunk = rem > SAR_PRESERVE_STAGE_MAX ? SAR_PRESERVE_STAGE_MAX : (ULONG)rem;
            SAR_STAGE_RESULT r;
            __try {
                r = SarPreserveStage(g_sar.preserve, Path, offset, chunk, actor,
                                     (PUCHAR)base + offset, chunk, FALSE);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                worst = SAR_STAGE_FAILED;
                break;
            }
            if (r == SAR_STAGE_FAILED || r == SAR_STAGE_DROPPED)
                worst = r;
        }
        ZwUnmapViewOfSection(ZwCurrentProcess(), base);
    }

    ZwClose(hSection);
    ObDereferenceObject(sectionObj);
    FltCloseSectionForDataScan(sectionCtx);
    FltReleaseContext(sectionCtx);
    return worst;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static VOID SarCaptureResolveProvenanceFo(_In_ PFLT_INSTANCE Instance, _In_ PFILE_OBJECT FileObject,
                                          _Out_writes_(SEMANTICS_AR_PROVENANCE_PATH_MAX) PUINT16 Out)
{
    PFLT_FILE_NAME_INFORMATION info = NULL;
    ULONG i;

    for (i = 0; i < SEMANTICS_AR_PROVENANCE_PATH_MAX; i++)
        Out[i] = 0;

    if (!NT_SUCCESS(FltGetFileNameInformationUnsafe(FileObject, Instance,
                                                    FLT_FILE_NAME_NORMALIZED |
                                                    FLT_FILE_NAME_QUERY_DEFAULT, &info)))
        return;
    if (NT_SUCCESS(FltParseFileNameInformation(info))) {
        ULONG chars = info->Name.Length / sizeof(WCHAR);
        if (chars > SEMANTICS_AR_PROVENANCE_PATH_MAX - 1)
            chars = SEMANTICS_AR_PROVENANCE_PATH_MAX - 1;
        for (i = 0; i < chars; i++)
            Out[i] = (UINT16)info->Name.Buffer[i];
        Out[chars] = 0;
    }
    FltReleaseFileNameInformation(info);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
SAR_STAGE_RESULT SarCaptureSubmitSupersede(_In_opt_ PSAR_CAPTURE_CTX Ctx, _In_ PFLT_CALLBACK_DATA Data,
                                           _In_ PCFLT_RELATED_OBJECTS FltObjects, _In_ PEPROCESS Originator,
                                           _In_ PETHREAD Thread)
{
    PFLT_FILE_NAME_INFORMATION info = NULL;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    HANDLE h = NULL;
    PFILE_OBJECT fo = NULL;
    UINT16 path[SEMANTICS_AR_PROVENANCE_PATH_MAX];
    SAR_STAGE_RESULT result = SAR_STAGE_STORED;

    UNREFERENCED_PARAMETER(Thread);

    if (Ctx == NULL || g_sar.preserve == NULL || !SarPreserveReady(g_sar.preserve))
        return SAR_STAGE_STORED;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || IoGetTopLevelIrp() != NULL)
        return SAR_STAGE_STORED;

    if (!NT_SUCCESS(FltGetFileNameInformation(Data, FLT_FILE_NAME_OPENED |
                                              FLT_FILE_NAME_QUERY_DEFAULT, &info)))
        return SAR_STAGE_STORED;
    if (!NT_SUCCESS(FltParseFileNameInformation(info))) {
        FltReleaseFileNameInformation(info);
        return SAR_STAGE_STORED;
    }

    InitializeObjectAttributes(&oa, &info->Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL,
                               NULL);
    if (!NT_SUCCESS(FltCreateFileEx(Ctx->filter, FltObjects->Instance, &h, &fo,
                                    FILE_READ_DATA | SYNCHRONIZE, &oa, &iosb, NULL,
                                    FILE_ATTRIBUTE_NORMAL,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    FILE_OPEN,
                                    FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
                                    NULL, 0, 0))) {
        FltReleaseFileNameInformation(info);
        return SAR_STAGE_STORED;
    }

    if (!SarCaptureStreamConfidentBlind(FltObjects->Instance, fo)) {
        SarCaptureResolveProvenanceFo(FltObjects->Instance, fo, path);
        if (path[0] != 0 && !SarCaptureProvenanceIsOwn(path))
            result = SarCaptureWholeContent(Ctx, FltObjects->Instance, fo, path, Originator);
    }

    FltClose(h);
    ObDereferenceObject(fo);
    FltReleaseFileNameInformation(info);
    return result;
}

#define SAR_MAP_GRANULARITY 0x10000ULL

_IRQL_requires_max_(PASSIVE_LEVEL)
static BOOLEAN SarCapRangeInsert(_Inout_ PSAR_STREAM_CONTEXT Sc, _In_ UINT64 Start, _In_ UINT64 End)
{
    ULONG i, w;
    PSAR_CAPTURED_RANGE arr;

    if (Sc->cap_count + 1 > Sc->cap_capacity) {
        ULONG ncap = Sc->cap_capacity ? Sc->cap_capacity * 2 : 8;
        PSAR_CAPTURED_RANGE narr = (PSAR_CAPTURED_RANGE)ExAllocatePool2(
            POOL_FLAG_NON_PAGED, (SIZE_T)ncap * sizeof(SAR_CAPTURED_RANGE), SAR_POOL_TAG_STREAMCTX);
        if (narr == NULL)
            return FALSE;
        if (Sc->cap_ranges != NULL) {
            RtlCopyMemory(narr, Sc->cap_ranges, (SIZE_T)Sc->cap_count * sizeof(SAR_CAPTURED_RANGE));
            ExFreePoolWithTag(Sc->cap_ranges, SAR_POOL_TAG_STREAMCTX);
        }
        Sc->cap_ranges = narr;
        Sc->cap_capacity = ncap;
    }

    arr = Sc->cap_ranges;
    for (i = 0; i < Sc->cap_count && arr[i].start < Start; i++)
        ;
    for (w = Sc->cap_count; w > i; w--)
        arr[w] = arr[w - 1];
    arr[i].start = Start;
    arr[i].end = End;
    Sc->cap_count++;

    w = 0;
    for (i = 0; i < Sc->cap_count; i++) {
        if (w > 0 && arr[i].start <= arr[w - 1].end) {
            if (arr[i].end > arr[w - 1].end)
                arr[w - 1].end = arr[i].end;
        } else {
            arr[w++] = arr[i];
        }
    }
    Sc->cap_count = w;
    return TRUE;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static SAR_STAGE_RESULT SarCaptureStageSpan(_In_ PSAR_PRESERVE Preserve, _In_ const UINT16 *Path,
                                            _In_ UINT64 Actor, _In_ PVOID Base,
                                            _In_ UINT64 AlignedStart, _In_ UINT64 SpanStart,
                                            _In_ UINT64 SpanEnd)
{
    UINT64 off;
    SAR_STAGE_RESULT worst = SAR_STAGE_STORED;
    for (off = SpanStart; off < SpanEnd; off += SAR_PRESERVE_STAGE_MAX) {
        UINT64 rem = SpanEnd - off;
        ULONG chunk = rem > SAR_PRESERVE_STAGE_MAX ? SAR_PRESERVE_STAGE_MAX : (ULONG)rem;
        SAR_STAGE_RESULT r;
        __try {
            r = SarPreserveStage(Preserve, Path, off, chunk, Actor,
                                 (PUCHAR)Base + (off - AlignedStart), chunk, FALSE);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return SAR_STAGE_FAILED;
        }
        if (r == SAR_STAGE_FAILED || r == SAR_STAGE_DROPPED)
            worst = r;
    }
    return worst;
}

SAR_STAGE_RESULT SarCaptureInPlaceRegion(_In_opt_ PSAR_CAPTURE_CTX Ctx, _In_ PFLT_CALLBACK_DATA Data,
                                         _In_ PCFLT_RELATED_OBJECTS FltObjects, _In_ PEPROCESS Originator,
                                         _In_ UINT64 Offset, _In_ UINT64 Length)
{
    UINT16 path[SEMANTICS_AR_PROVENANCE_PATH_MAX];
    PSAR_STREAM_CONTEXT sc = NULL;
    PFLT_CONTEXT sectionCtx = NULL;
    HANDLE hSection = NULL;
    PVOID sectionObj = NULL;
    LARGE_INTEGER sectionFileSize;
    OBJECT_ATTRIBUTES oa;
    PVOID base = NULL;
    SIZE_T viewSize;
    LARGE_INTEGER viewOffset;
    UINT64 end, fileSize, alignedStart, actor, cursor;
    SAR_STAGE_RESULT worst = SAR_STAGE_STORED;
    ULONG i;

    if (Ctx == NULL || g_sar.preserve == NULL || !SarPreserveReady(g_sar.preserve))
        return SAR_STAGE_STORED;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return SAR_STAGE_STORED;
    if (Length == 0)
        return SAR_STAGE_STORED;

    SarCaptureResolveProvenance(Data, path);
    if (SarCapturePathExempt(path))
        return SAR_STAGE_STORED;

    if (!NT_SUCCESS(FltGetStreamContext(FltObjects->Instance, FltObjects->FileObject,
                                        (PFLT_CONTEXT *)&sc)))
        return SAR_STAGE_STORED;

    end = Offset + Length;

    if (!NT_SUCCESS(FltAllocateContext(Ctx->filter, FLT_SECTION_CONTEXT, sizeof(LONG),
                                       PagedPool, &sectionCtx))) {
        FltReleaseContext(sc);
        return SAR_STAGE_STORED;
    }
    InitializeObjectAttributes(&oa, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    sectionFileSize.QuadPart = 0;
    if (!NT_SUCCESS(FltCreateSectionForDataScan(FltObjects->Instance, FltObjects->FileObject,
                                                sectionCtx, FILE_READ_DATA, &oa, NULL,
                                                PAGE_READONLY, SEC_COMMIT, 0,
                                                &hSection, &sectionObj, &sectionFileSize))) {
        FltReleaseContext(sectionCtx);
        FltReleaseContext(sc);
        return SAR_STAGE_STORED;
    }

    fileSize = (UINT64)sectionFileSize.QuadPart;
    if (end > fileSize)
        end = fileSize;

    if (end > Offset) {
        alignedStart = Offset & ~(SAR_MAP_GRANULARITY - 1);
        viewOffset.QuadPart = (LONGLONG)alignedStart;
        viewSize = (SIZE_T)(end - alignedStart);
        if (NT_SUCCESS(ZwMapViewOfSection(hSection, ZwCurrentProcess(), &base, 0, 0, &viewOffset,
                                          &viewSize, ViewUnmap, 0, PAGE_READONLY))) {
            actor = (UINT64)(ULONG_PTR)PsGetProcessId(Originator);

            FltAcquirePushLockExclusive(&sc->cap_lock);
            cursor = Offset;
            worst = SAR_STAGE_STORED;
            for (i = 0; i < sc->cap_count && cursor < end; i++) {
                UINT64 rs = sc->cap_ranges[i].start;
                UINT64 re = sc->cap_ranges[i].end;
                if (re <= cursor)
                    continue;
                if (rs >= end)
                    break;
                if (rs > cursor) {
                    SAR_STAGE_RESULT r = SarCaptureStageSpan(g_sar.preserve, path, actor, base,
                                                             alignedStart, cursor, rs < end ? rs : end);
                    if (r == SAR_STAGE_FAILED || r == SAR_STAGE_DROPPED)
                        worst = r;
                }
                if (re > cursor)
                    cursor = re;
            }
            if (cursor < end) {
                SAR_STAGE_RESULT r = SarCaptureStageSpan(g_sar.preserve, path, actor, base,
                                                         alignedStart, cursor, end);
                if (r == SAR_STAGE_FAILED || r == SAR_STAGE_DROPPED)
                    worst = r;
            }
            if (worst == SAR_STAGE_STORED)
                (VOID)SarCapRangeInsert(sc, Offset, end);
            FltReleasePushLock(&sc->cap_lock);

            ZwUnmapViewOfSection(ZwCurrentProcess(), base);
        }
    }

    ZwClose(hSection);
    ObDereferenceObject(sectionObj);
    FltCloseSectionForDataScan(sectionCtx);
    FltReleaseContext(sectionCtx);
    FltReleaseContext(sc);
    return worst;
}

typedef struct _SAR_MMAP_ASYNC {
    KEVENT event;
    IO_STATUS_BLOCK iosb;
    PUCHAR buffer;
    volatile LONG ref;
} SAR_MMAP_ASYNC, *PSAR_MMAP_ASYNC;

static volatile LONG64 g_sar_mmap_miss = 0;

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarMmapInstanceResolve(_In_ PFLT_FILTER Filter, _In_ PCFLT_RELATED_OBJECTS FltObjects)
{
    PDEVICE_OBJECT diskObj = NULL;
    PDEVICE_OBJECT top = NULL;
    HANDLE volHandle = NULL;
    PFILE_OBJECT volFo = NULL;
    NTFS_VOLUME_DATA_BUFFER volData;
    IO_STATUS_BLOCK iosb;
    ULONG bpc = 0;
    ULONG bps = 0;
    PSAR_MMAP_INSTCTX ic = NULL;

    if (FltObjects->Volume == NULL)
        return;
    if (!NT_SUCCESS(FltGetDiskDeviceObject(FltObjects->Volume, &diskObj)) || diskObj == NULL)
        return;
    top = IoGetAttachedDeviceReference(diskObj);
    ObDereferenceObject(diskObj);
    if (top == NULL)
        return;

    if (NT_SUCCESS(FltOpenVolume(FltObjects->Instance, &volHandle, &volFo))) {
        RtlZeroMemory(&volData, sizeof(volData));
        if (NT_SUCCESS(ZwFsControlFile(volHandle, NULL, NULL, NULL, &iosb, FSCTL_GET_NTFS_VOLUME_DATA,
                                       NULL, 0, &volData, sizeof(volData)))) {
            bpc = volData.BytesPerCluster;
            bps = volData.BytesPerSector;
        }
        FltClose(volHandle);
        ObDereferenceObject(volFo);
    }

    if (bpc == 0) {
        ObDereferenceObject(top);
        return;
    }
    if (bps == 0)
        bps = 512;

    if (!NT_SUCCESS(FltAllocateContext(Filter, FLT_INSTANCE_CONTEXT, sizeof(*ic), NonPagedPoolNx,
                                       (PFLT_CONTEXT *)&ic))) {
        ObDereferenceObject(top);
        return;
    }
    ic->disk_top = top;
    ic->bytes_per_cluster = bpc;
    ic->bytes_per_sector = bps;
    if (!NT_SUCCESS(FltSetInstanceContext(FltObjects->Instance, FLT_SET_CONTEXT_KEEP_IF_EXISTS, ic,
                                          NULL)))
        ObDereferenceObject(top);
    FltReleaseContext(ic);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarMmapInstanceCleanup(_In_ PFLT_CONTEXT Context, _In_ FLT_CONTEXT_TYPE ContextType)
{
    PSAR_MMAP_INSTCTX ic = (PSAR_MMAP_INSTCTX)Context;

    UNREFERENCED_PARAMETER(ContextType);

    if (ic->disk_top != NULL) {
        ObDereferenceObject(ic->disk_top);
        ic->disk_top = NULL;
    }
}

static VOID SarMmapAsyncRelease(_In_ PSAR_MMAP_ASYNC C)
{
    if (InterlockedDecrement(&C->ref) == 0) {
        if (C->buffer != NULL)
            ExFreePoolWithTag(C->buffer, SAR_POOL_TAG_STREAMCTX);
        ExFreePoolWithTag(C, SAR_POOL_TAG_STREAMCTX);
    }
}

_Function_class_(IO_COMPLETION_ROUTINE)
static NTSTATUS SarMmapAsyncDone(_In_ PDEVICE_OBJECT Dev, _In_ PIRP Irp, _In_ PVOID Context)
{
    PSAR_MMAP_ASYNC c = (PSAR_MMAP_ASYNC)Context;

    UNREFERENCED_PARAMETER(Dev);

    c->iosb = Irp->IoStatus;
    if (Irp->MdlAddress != NULL) {
        MmUnlockPages(Irp->MdlAddress);
        IoFreeMdl(Irp->MdlAddress);
        Irp->MdlAddress = NULL;
    }
    KeSetEvent(&c->event, IO_NO_INCREMENT, FALSE);
    IoFreeIrp(Irp);
    SarMmapAsyncRelease(c);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static BOOLEAN SarMmapDiskRead(_In_ PDEVICE_OBJECT Top, _In_ LONGLONG DiskOffset, _In_ ULONG Length,
                               _In_ ULONG DeadmanMs, _Out_writes_bytes_(Length) PUCHAR Out)
{
    PSAR_MMAP_ASYNC c;
    PIRP irp;
    LARGE_INTEGER off, to;
    NTSTATUS w;
    BOOLEAN ok = FALSE;

    if (Top == NULL)
        return FALSE;

    c = (PSAR_MMAP_ASYNC)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*c), SAR_POOL_TAG_STREAMCTX);
    if (c == NULL)
        return FALSE;
    c->buffer = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, Length, SAR_POOL_TAG_STREAMCTX);
    if (c->buffer == NULL) {
        ExFreePoolWithTag(c, SAR_POOL_TAG_STREAMCTX);
        return FALSE;
    }
    KeInitializeEvent(&c->event, NotificationEvent, FALSE);
    RtlZeroMemory(&c->iosb, sizeof(c->iosb));
    c->ref = 2;

    off.QuadPart = DiskOffset;
    irp = IoBuildAsynchronousFsdRequest(IRP_MJ_READ, Top, c->buffer, Length, &off, &c->iosb);
    if (irp == NULL) {
        SarMmapAsyncRelease(c);
        SarMmapAsyncRelease(c);
        return FALSE;
    }
    IoSetCompletionRoutine(irp, SarMmapAsyncDone, c, TRUE, TRUE, TRUE);
    (VOID)IoCallDriver(Top, irp);

    to.QuadPart = -((LONGLONG)DeadmanMs * 10 * 1000LL);
    w = KeWaitForSingleObject(&c->event, Executive, KernelMode, FALSE, &to);
    if (w == STATUS_SUCCESS && NT_SUCCESS(c->iosb.Status) && (ULONG)c->iosb.Information >= Length) {
        RtlCopyMemory(Out, c->buffer, Length);
        ok = TRUE;
    }
    SarMmapAsyncRelease(c);
    return ok;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static PSAR_EXTENT_MAP SarBuildExtentMap(_In_ PFLT_INSTANCE Instance, _In_ PFILE_OBJECT FileObject,
                                         _In_ ULONG Bpc, _In_ ULONG Bps)
{
    STARTING_VCN_INPUT_BUFFER vcnIn;
    ULONG retCap = sizeof(RETRIEVAL_POINTERS_BUFFER) + 64u * 2u * sizeof(LARGE_INTEGER);
    PRETRIEVAL_POINTERS_BUFFER ret;
    PSAR_EXTENT_MAP map;
    ULONG mapCap = 16;
    ULONG count = 0;
    UINT64 coveredVcn = 0;
    NTSTATUS status;

    ret = (PRETRIEVAL_POINTERS_BUFFER)ExAllocatePool2(POOL_FLAG_PAGED, retCap, SAR_POOL_TAG_STREAMCTX);
    if (ret == NULL)
        return NULL;
    map = (PSAR_EXTENT_MAP)ExAllocatePool2(POOL_FLAG_NON_PAGED,
              sizeof(SAR_EXTENT_MAP) + (mapCap - 1) * sizeof(SAR_EXTENT), SAR_POOL_TAG_STREAMCTX);
    if (map == NULL) {
        ExFreePoolWithTag(ret, SAR_POOL_TAG_STREAMCTX);
        return NULL;
    }

    vcnIn.StartingVcn.QuadPart = 0;
    for (;;) {
        UINT64 prevVcn;
        ULONG k;
        status = FltFsControlFile(Instance, FileObject, FSCTL_GET_RETRIEVAL_POINTERS, &vcnIn,
                                  sizeof(vcnIn), ret, retCap, NULL);
        if (!NT_SUCCESS(status) && status != STATUS_BUFFER_OVERFLOW)
            break;
        prevVcn = (UINT64)ret->StartingVcn.QuadPart;
        for (k = 0; k < ret->ExtentCount; k++) {
            UINT64 nextVcn = (UINT64)ret->Extents[k].NextVcn.QuadPart;
            INT64 lcn = ret->Extents[k].Lcn.QuadPart;
            if (count >= mapCap) {
                ULONG ncap = mapCap * 2;
                PSAR_EXTENT_MAP nm = (PSAR_EXTENT_MAP)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                    sizeof(SAR_EXTENT_MAP) + (ncap - 1) * sizeof(SAR_EXTENT), SAR_POOL_TAG_STREAMCTX);
                if (nm == NULL)
                    goto done;
                RtlCopyMemory(nm, map, sizeof(SAR_EXTENT_MAP) + (mapCap - 1) * sizeof(SAR_EXTENT));
                ExFreePoolWithTag(map, SAR_POOL_TAG_STREAMCTX);
                map = nm;
                mapCap = ncap;
            }
            map->ext[count].vcn_byte = prevVcn * Bpc;
            map->ext[count].len = (nextVcn - prevVcn) * Bpc;
            map->ext[count].lcn_byte = (lcn < 0) ? -1 : lcn * (INT64)Bpc;
            count++;
            coveredVcn = nextVcn;
            prevVcn = nextVcn;
        }
        if (status == STATUS_BUFFER_OVERFLOW && ret->ExtentCount > 0) {
            vcnIn.StartingVcn.QuadPart = (LONGLONG)coveredVcn;
            continue;
        }
        break;
    }
done:
    ExFreePoolWithTag(ret, SAR_POOL_TAG_STREAMCTX);
    if (count == 0) {
        ExFreePoolWithTag(map, SAR_POOL_TAG_STREAMCTX);
        return NULL;
    }
    map->count = count;
    map->sector_size = Bps ? Bps : 512;
    map->covered_len = coveredVcn * Bpc;
    return map;
}

static const SAR_EXTENT *SarFindExtent(_In_ const SAR_EXTENT_MAP *Map, _In_ UINT64 Pos)
{
    ULONG i;
    for (i = 0; i < Map->count; i++)
        if (Pos >= Map->ext[i].vcn_byte && Pos < Map->ext[i].vcn_byte + Map->ext[i].len)
            return &Map->ext[i];
    return NULL;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static VOID SarMmapResolvePath(_In_ PFLT_INSTANCE Instance, _In_ PFILE_OBJECT FileObject,
                               _Inout_ PSAR_STREAM_CONTEXT Sc)
{
    PFLT_FILE_NAME_INFORMATION info = NULL;

    if (Sc->mmap_path != NULL)
        return;
    if (!NT_SUCCESS(FltGetFileNameInformationUnsafe(FileObject, Instance,
                                                    FLT_FILE_NAME_NORMALIZED |
                                                    FLT_FILE_NAME_QUERY_DEFAULT, &info)))
        return;
    if (NT_SUCCESS(FltParseFileNameInformation(info))) {
        PUINT16 pathbuf = (PUINT16)ExAllocatePool2(
            POOL_FLAG_NON_PAGED, SEMANTICS_AR_PROVENANCE_PATH_MAX * sizeof(UINT16),
            SAR_POOL_TAG_STREAMCTX);
        if (pathbuf != NULL) {
            ULONG chars = info->Name.Length / sizeof(WCHAR);
            ULONG i;
            if (chars > SEMANTICS_AR_PROVENANCE_PATH_MAX - 1)
                chars = SEMANTICS_AR_PROVENANCE_PATH_MAX - 1;
            for (i = 0; i < chars; i++)
                pathbuf[i] = (UINT16)info->Name.Buffer[i];
            pathbuf[chars] = 0;
            if (InterlockedCompareExchangePointer((PVOID *)&Sc->mmap_path, pathbuf, NULL) != NULL)
                ExFreePoolWithTag(pathbuf, SAR_POOL_TAG_STREAMCTX);
        }
    }
    FltReleaseFileNameInformation(info);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static VOID SarMmapResolveMap(_In_ PFLT_INSTANCE Instance, _In_ PFILE_OBJECT FileObject,
                              _Inout_ PSAR_STREAM_CONTEXT Sc)
{
    PSAR_MMAP_INSTCTX ic = NULL;
    ULONG bpc, bps;
    PSAR_EXTENT_MAP map;

    if (Sc->mmap_map != NULL || Sc->mmap_path == NULL)
        return;
    if (!NT_SUCCESS(FltGetInstanceContext(Instance, (PFLT_CONTEXT *)&ic)))
        return;
    bpc = ic->bytes_per_cluster;
    bps = ic->bytes_per_sector;
    FltReleaseContext(ic);
    if (bpc == 0)
        return;
    map = SarBuildExtentMap(Instance, FileObject, bpc, bps);
    if (map == NULL)
        return;
    if (InterlockedCompareExchangePointer((PVOID *)&Sc->mmap_map, map, NULL) != NULL)
        ExFreePoolWithTag(map, SAR_POOL_TAG_STREAMCTX);
}

static BOOLEAN SarWidePrefixI(_In_ const UINT16 *Str, _In_ PCWSTR Prefix)
{
    ULONG j;
    for (j = 0; Prefix[j] != 0; j++) {
        WCHAR a = (WCHAR)Str[j];
        WCHAR b = Prefix[j];
        if (a == 0)
            return FALSE;
        if (a >= L'a' && a <= L'z') a = (WCHAR)(a - 32);
        if (b >= L'a' && b <= L'z') b = (WCHAR)(b - 32);
        if (a != b)
            return FALSE;
    }
    return TRUE;
}

static BOOLEAN SarMmapPathExcluded(_In_ const UINT16 *Path)
{
    ULONG i, bs = 0, pos = 0;

    if (Path == NULL)
        return FALSE;
    for (i = 0; Path[i] != 0; i++) {
        if (Path[i] == L'\\') {
            if (++bs == 3) { pos = i + 1; break; }
        }
    }
    if (pos == 0)
        return FALSE;
    return SarWidePrefixI(&Path[pos], L"Windows\\") ||
           SarWidePrefixI(&Path[pos], L"ProgramData\\Microsoft\\");
}

VOID SarMmapArm(_In_ PFLT_INSTANCE Instance, _In_ PFILE_OBJECT FileObject,
                _Inout_ PSAR_STREAM_CONTEXT Sc, _In_ HANDLE ArmPid)
{
    if (Sc->mmap_arm_pid == NULL)
        InterlockedCompareExchangePointer((PVOID *)&Sc->mmap_arm_pid, ArmPid, NULL);
    if (Sc->mmap_map != NULL)
        return;
    SarMmapResolvePath(Instance, FileObject, Sc);
    if (Sc->mmap_path != NULL && SarMmapPathExcluded(Sc->mmap_path))
        return;
    SarMmapResolveMap(Instance, FileObject, Sc);
}

#define SAR_RAW_CHUNK (256u * 1024u)

_IRQL_requires_max_(PASSIVE_LEVEL)
static VOID SarMmapReleaseStaged(_Inout_ volatile LONG64 *StreamReserved, _In_ UINT64 Bytes)
{
    for (;;) {
        LONG64 cur = *StreamReserved;
        UINT64 rel;
        if (cur <= 0)
            return;
        rel = (UINT64)cur < Bytes ? (UINT64)cur : Bytes;
        if (InterlockedCompareExchange64(StreamReserved, cur - (LONG64)rel, cur) == cur) {
            SarPreserveRelease(g_sar.preserve, rel);
            return;
        }
    }
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static SAR_STAGE_RESULT SarRawReadStageRange(_In_ PDEVICE_OBJECT Top, _In_ const SAR_EXTENT_MAP *Map,
                                             _In_ const UINT16 *Path, _In_ UINT64 Actor,
                                             _In_ UINT64 Offset, _In_ ULONG Length, _In_ BOOLEAN Backed,
                                             _In_opt_ volatile LONG64 *StreamReserved,
                                             _Out_writes_bytes_opt_(PreHeadCap) PUCHAR PreHead,
                                             _In_ ULONG PreHeadCap, _Inout_opt_ PULONG PreHeadLen)
{
    UINT64 cursor = Offset;
    UINT64 logEnd = Offset + Length;
    ULONG sector = Map->sector_size ? Map->sector_size : 512;
    SAR_STAGE_RESULT worst = SAR_STAGE_STORED;

    while (cursor < logEnd) {
        const SAR_EXTENT *ex = SarFindExtent(Map, cursor);
        UINT64 extEnd, step;
        SAR_STAGE_RESULT r;

        if (ex == NULL) {
            InterlockedIncrement64(&g_sar_mmap_miss);
            return SAR_STAGE_FAILED;
        }
        extEnd = ex->vcn_byte + ex->len;
        step = logEnd - cursor;
        if (step > extEnd - cursor)
            step = extEnd - cursor;
        if (step > SAR_RAW_CHUNK)
            step = SAR_RAW_CHUNK;

        if (ex->lcn_byte < 0) {
            PUCHAR z = (PUCHAR)ExAllocatePool2(POOL_FLAG_PAGED, (SIZE_T)step, SAR_POOL_TAG_PRESBUF);
            if (z == NULL)
                return SAR_STAGE_FAILED;
            r = SarPreserveStage(g_sar.preserve, Path, cursor, step, Actor, z, (ULONG)step, Backed);
            if (PreHead != NULL && PreHeadLen != NULL && *PreHeadLen < PreHeadCap) {
                ULONG want = (ULONG)step;
                if (want > PreHeadCap - *PreHeadLen)
                    want = PreHeadCap - *PreHeadLen;
                RtlCopyMemory(PreHead + *PreHeadLen, z, want);
                *PreHeadLen += want;
            }
            ExFreePoolWithTag(z, SAR_POOL_TAG_PRESBUF);
        } else {
            UINT64 diskOff = (UINT64)ex->lcn_byte + (cursor - ex->vcn_byte);
            UINT64 aStart = diskOff & ~((UINT64)sector - 1);
            UINT64 aEnd = (diskOff + step + sector - 1) & ~((UINT64)sector - 1);
            ULONG readLen = (ULONG)(aEnd - aStart);
            PUCHAR scratch = (PUCHAR)ExAllocatePool2(POOL_FLAG_PAGED, readLen, SAR_POOL_TAG_PRESBUF);
            if (scratch == NULL)
                return SAR_STAGE_FAILED;
            if (!SarMmapDiskRead(Top, (LONGLONG)aStart, readLen, 20000, scratch)) {
                ExFreePoolWithTag(scratch, SAR_POOL_TAG_PRESBUF);
                InterlockedIncrement64(&g_sar_mmap_miss);
                return SAR_STAGE_FAILED;
            }
            r = SarPreserveStage(g_sar.preserve, Path, cursor, step, Actor,
                                 scratch + (diskOff - aStart), (ULONG)step, Backed);
            if (PreHead != NULL && PreHeadLen != NULL && *PreHeadLen < PreHeadCap) {
                ULONG want = (ULONG)step;
                if (want > PreHeadCap - *PreHeadLen)
                    want = PreHeadCap - *PreHeadLen;
                RtlCopyMemory(PreHead + *PreHeadLen, scratch + (diskOff - aStart), want);
                *PreHeadLen += want;
            }
            ExFreePoolWithTag(scratch, SAR_POOL_TAG_PRESBUF);
        }
        if (r == SAR_STAGE_STORED && Backed && StreamReserved != NULL)
            SarMmapReleaseStaged(StreamReserved, ((UINT64)step + 15) & ~15ULL);
        if (r == SAR_STAGE_FAILED || r == SAR_STAGE_DROPPED)
            worst = r;
        cursor += step;
    }
    return worst;
}

_IRQL_requires_max_(APC_LEVEL)
static BOOLEAN SarMmapRangeCovered(_In_ PSAR_STREAM_CONTEXT Sc, _In_ UINT64 Offset, _In_ ULONG Length)
{
    ULONG i;
    BOOLEAN covered = FALSE;

    FltAcquirePushLockShared(&Sc->cap_lock);
    for (i = 0; i < Sc->cap_count; i++) {
        if (Sc->cap_ranges[i].start <= Offset && Offset + Length <= Sc->cap_ranges[i].end) {
            covered = TRUE;
            break;
        }
    }
    FltReleasePushLock(&Sc->cap_lock);
    return covered;
}

_IRQL_requires_max_(APC_LEVEL)
static SAR_STAGE_RESULT SarMmapCaptureInline(_In_ PFLT_CALLBACK_DATA Data,
                                             _In_ PCFLT_RELATED_OBJECTS FltObjects,
                                             _In_ PSAR_STREAM_CONTEXT Sc,
                                             _In_ UINT64 Actor, _In_ UINT64 Offset, _In_ ULONG Length)
{
    PSAR_MMAP_INSTCTX ic = NULL;
    PDEVICE_OBJECT top;
    SAR_STAGE_RESULT r = SAR_STAGE_FAILED;
    const SAR_EXTENT *startExt;
    BOOLEAN oracleEligible;
    PUCHAR preHead = NULL;
    ULONG preHeadLen = 0;
    SAR_WRITE_SEAM_REQUEST req;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return SAR_STAGE_FAILED;
    if (Sc->mmap_map == NULL || Sc->mmap_path == NULL)
        return SAR_STAGE_FAILED;
    if (Offset + Length > Sc->mmap_map->covered_len)
        return SAR_STAGE_STORED;
    if (SarMmapRangeCovered(Sc, Offset, Length))
        return SAR_STAGE_ALREADY_COVERED;
    if (!NT_SUCCESS(FltGetInstanceContext(FltObjects->Instance, (PFLT_CONTEXT *)&ic)))
        return SAR_STAGE_FAILED;
    top = ic->disk_top;
    FltReleaseContext(ic);
    if (top == NULL)
        return SAR_STAGE_FAILED;

    if (SarStateModeGet(g_sar_state) == SEMANTICS_AR_MODE_ENFORCE &&
        InterlockedCompareExchange64(&Sc->mmap_reserved, 0, 0) == 0 &&
        (Sc->flags & SAR_STREAMCTX_FLAG_MMAP_RESERVE_TRIED) == 0) {
        UINT64 est = Sc->mmap_map->covered_len + Sc->mmap_map->covered_len / 16;
        InterlockedOr(&Sc->flags, (LONG)SAR_STREAMCTX_FLAG_MMAP_RESERVE_TRIED);
        if (SarPreserveReserve(g_sar.preserve, est)) {
            if (InterlockedCompareExchange64(&Sc->mmap_reserved, (LONG64)est, 0) != 0)
                SarPreserveRelease(g_sar.preserve, est);
        }
    }

    RtlZeroMemory(&req, sizeof(req));
    startExt = SarFindExtent(Sc->mmap_map, Offset);
    oracleEligible = startExt != NULL && startExt->lcn_byte >= 0 && Length >= SAR_CANDIDATE_SIZE;
    if (oracleEligible)
        preHead = req.pre_image;

    __try {
        BOOLEAN backed = InterlockedCompareExchange64(&Sc->mmap_reserved, 0, 0) != 0;
        r = SarRawReadStageRange(top, Sc->mmap_map, Sc->mmap_path, Actor, Offset, Length, backed,
                                 &Sc->mmap_reserved, preHead, SAR_CAPTURE_BUFFER_BYTES, &preHeadLen);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        r = SAR_STAGE_FAILED;
    }
    if (r == SAR_STAGE_STORED || r == SAR_STAGE_ALREADY_COVERED) {
        FltAcquirePushLockExclusive(&Sc->cap_lock);
        (VOID)SarCapRangeInsert(Sc, Offset, Offset + Length);
        FltReleasePushLock(&Sc->cap_lock);
    }

    if (oracleEligible && r == SAR_STAGE_STORED && preHeadLen >= SAR_CANDIDATE_SIZE) {
        PEPROCESS proc = NULL;
        if (NT_SUCCESS(PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)Actor, &proc))) {
            PETHREAD thr = PsGetCurrentThread();
            ObReferenceObject(thr);
            req.data = Data;
            req.related = FltObjects;
            req.originator_process = proc;
            req.originator_thread = thr;
            req.member = SAR_DESTRUCT_WRITE_NONCACHED;
            req.irp_flags = Data->Iopb->IrpFlags;
            req.write_offset.QuadPart = (LONGLONG)Offset;
            req.write_length = Length;
            req.pre_image_len = preHeadLen;
            req.provenance_override = Sc->mmap_path;
            SarCaptureSubmitWrite(g_sar.capture, &req);
        }
    }
    return r;
}

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarMmapOnPagingWrite(_In_opt_ PSAR_CAPTURE_CTX Ctx, _In_ PCFLT_RELATED_OBJECTS FltObjects,
                             _In_ PFLT_CALLBACK_DATA Data)
{
    PSAR_STREAM_CONTEXT sc = NULL;
    UINT64 offset;
    ULONG length;
    BOOLEAN want;

    if (Ctx == NULL || g_sar.preserve == NULL || !SarPreserveReady(g_sar.preserve))
        return FALSE;
    if (Data->Iopb == NULL || FltObjects->FileObject == NULL)
        return FALSE;
    if ((Data->Iopb->IrpFlags & (IRP_PAGING_IO | IRP_SYNCHRONOUS_PAGING_IO)) == 0)
        return FALSE;

    offset = (UINT64)Data->Iopb->Parameters.Write.ByteOffset.QuadPart;
    length = Data->Iopb->Parameters.Write.Length;
    if (length == 0)
        return FALSE;

    if (!NT_SUCCESS(FltGetStreamContext(FltObjects->Instance, FltObjects->FileObject,
                                        (PFLT_CONTEXT *)&sc)))
        return FALSE;

    want = (sc->flags & SAR_STREAMCTX_FLAG_SECTION_DIRTY) != 0 &&
           (sc->flags & SAR_STREAMCTX_FLAG_OWN_STORE) == 0 &&
           (sc->flags & SAR_STREAMCTX_FLAG_PHANTOM_BACKING) == 0 &&
           sc->mmap_map != NULL;
    if (want)
        SarMmapCaptureInline(Data, FltObjects, sc, (UINT64)(ULONG_PTR)sc->mmap_arm_pid,
                             offset, length);
    FltReleaseContext(sc);
    return want;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarMmapCaptureNocache(_In_opt_ PSAR_CAPTURE_CTX Ctx, _In_ PCFLT_RELATED_OBJECTS FltObjects,
                           _In_ PFLT_CALLBACK_DATA Data, _In_ PEPROCESS Originator)
{
    PSAR_STREAM_CONTEXT sc = NULL;
    UINT64 offset;
    ULONG length;

    if (Ctx == NULL || g_sar.preserve == NULL || !SarPreserveReady(g_sar.preserve))
        return;
    if (Data->Iopb == NULL || FltObjects->FileObject == NULL)
        return;

    offset = (UINT64)Data->Iopb->Parameters.Write.ByteOffset.QuadPart;
    length = Data->Iopb->Parameters.Write.Length;
    if (length == 0)
        return;

    if (!NT_SUCCESS(FltGetStreamContext(FltObjects->Instance, FltObjects->FileObject,
                                        (PFLT_CONTEXT *)&sc)))
        return;
    if ((sc->flags & SAR_STREAMCTX_FLAG_SECTION_DIRTY) != 0 &&
        (sc->flags & SAR_STREAMCTX_FLAG_OWN_STORE) == 0 &&
        (sc->flags & SAR_STREAMCTX_FLAG_PHANTOM_BACKING) == 0 &&
        sc->mmap_map != NULL)
        SarMmapCaptureInline(Data, FltObjects, sc,
                             (UINT64)(ULONG_PTR)PsGetProcessId(Originator), offset, length);
    FltReleaseContext(sc);
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
    FltInitializePushLock(&ctx->scanned_lock);
    ExInitializeRundownProtection(&ctx->rundown);

    ctx->scanned_count = 0;
    ctx->scanned = (PEPROCESS *)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                                SAR_SCANNED_CAPACITY * sizeof(PEPROCESS),
                                                SAR_POOL_TAG_FLAGS);
    if (ctx->scanned == NULL) {
        FltDeletePushLock(&ctx->scanned_lock);
        FltDeletePushLock(&ctx->blocked_lock);
        ExDeleteLookasideListEx(&ctx->work_pool);
        ExFreePoolWithTag(ctx->blocked, SAR_POOL_TAG_BLOCKED);
        ExFreePoolWithTag(ctx, SAR_POOL_TAG_CAPCTX);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

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

    for (i = 0; i < Ctx->scanned_count; i++)
        ObDereferenceObject(Ctx->scanned[i]);

    if (Ctx->work_pool_init)
        ExDeleteLookasideListEx(&Ctx->work_pool);

    FltDeletePushLock(&Ctx->blocked_lock);
    FltDeletePushLock(&Ctx->scanned_lock);

    if (Ctx->blocked != NULL)
        ExFreePoolWithTag(Ctx->blocked, SAR_POOL_TAG_BLOCKED);
    if (Ctx->scanned != NULL)
        ExFreePoolWithTag(Ctx->scanned, SAR_POOL_TAG_FLAGS);

    ExFreePoolWithTag(Ctx, SAR_POOL_TAG_CAPCTX);
}
