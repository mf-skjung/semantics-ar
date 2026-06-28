#include "phantom.h"
#include "state.h"
#include "seam.h"
#include "sha256.h"
#include "eng_mem.h"
#include "store_io.h"

extern SAR_GLOBALS g_sar;
extern PSAR_STATE g_sar_state;

typedef NTSTATUS (*PIO_REPLACE_FILE_OBJECT_NAME_FN)(
    _In_ PFILE_OBJECT FileObject,
    _In_reads_bytes_(FileNameLength) PWSTR NewFileName,
    _In_ USHORT FileNameLength);

static PIO_REPLACE_FILE_OBJECT_NAME_FN g_sar_io_replace_name;

static const SAR_DIR_OFFSETS g_dir_offsets_dir = {
    0, 4, 8, 16, 24, 32, 40, 48, 56, 60, 64, -1, -1, -1, -1, 64
};
static const SAR_DIR_OFFSETS g_dir_offsets_full = {
    0, 4, 8, 16, 24, 32, 40, 48, 56, 60, 68, 64, -1, -1, -1, 68
};
static const SAR_DIR_OFFSETS g_dir_offsets_both = {
    0, 4, 8, 16, 24, 32, 40, 48, 56, 60, 94, 64, 68, 70, -1, 94
};
static const SAR_DIR_OFFSETS g_dir_offsets_names = {
    0, 4, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 8, 12, -1, -1, -1, -1, 12
};
static const SAR_DIR_OFFSETS g_dir_offsets_id_full = {
    0, 4, 8, 16, 24, 32, 40, 48, 56, 60, 80, 64, -1, -1, 72, 80
};
static const SAR_DIR_OFFSETS g_dir_offsets_id_both = {
    0, 4, 8, 16, 24, 32, 40, 48, 56, 60, 104, 64, 68, 70, 96, 104
};

static const SAR_DIR_OFFSETS *SarPhantomDirOffsets(FILE_INFORMATION_CLASS cls)
{
    switch (cls) {
    case FileDirectoryInformation:       return &g_dir_offsets_dir;
    case FileFullDirectoryInformation:   return &g_dir_offsets_full;
    case FileBothDirectoryInformation:   return &g_dir_offsets_both;
    case FileNamesInformation:           return &g_dir_offsets_names;
    case FileIdFullDirectoryInformation: return &g_dir_offsets_id_full;
    case FileIdBothDirectoryInformation: return &g_dir_offsets_id_both;
    default: return NULL;
    }
}

static ULONG SarPhantomEntrySize(const SAR_DIR_OFFSETS *off, ULONG name_bytes)
{
    ULONG size = off->base_record_size + name_bytes;
    return (size + 7) & ~7u;
}

static VOID SarPhantomDeriveSecret(_In_ const UCHAR MacKey[32],
                                   _Out_writes_(32) UCHAR Secret[32])
{
    static const UINT8 label[] = "phantom-volume-secret";
    sar_hmac_sha256(MacKey, 32, label, sizeof(label) - 1, Secret);
}

static VOID SarPhantomNameForIndex(_In_ const UCHAR Secret[32],
                                   _In_ PCUNICODE_STRING DirPath,
                                   _In_ ULONG Index,
                                   _Out_writes_(SAR_PHANTOM_NAME_CHARS) WCHAR *NameBuf,
                                   _Out_ PUSHORT NameChars)
{
    UINT8 msg[520 + 4];
    ULONG dir_bytes;
    UINT8 hash[32];
    static const WCHAR hex[] = L"0123456789abcdef";
    ULONG i;

    dir_bytes = DirPath->Length;
    if (dir_bytes > 520)
        dir_bytes = 520;
    RtlCopyMemory(msg, DirPath->Buffer, dir_bytes);
    msg[dir_bytes]     = (UINT8)(Index & 0xFF);
    msg[dir_bytes + 1] = (UINT8)((Index >> 8) & 0xFF);
    msg[dir_bytes + 2] = (UINT8)((Index >> 16) & 0xFF);
    msg[dir_bytes + 3] = (UINT8)((Index >> 24) & 0xFF);

    sar_hmac_sha256(Secret, 32, msg, dir_bytes + 4, hash);

    for (i = 0; i < 12; i++) {
        NameBuf[i * 2]     = hex[(hash[i] >> 4) & 0xF];
        NameBuf[i * 2 + 1] = hex[hash[i] & 0xF];
    }
    NameBuf[24] = L'.';
    NameBuf[25] = L'd';
    NameBuf[26] = L'o';
    NameBuf[27] = L'c';
    NameBuf[28] = L'x';
    *NameChars = 29;
}

static ULONG SarPhantomCountForDir(ULONG real_file_count)
{
    if (real_file_count == 0) return 0;
    if (real_file_count <= 5) return 1;
    if (real_file_count <= 20) return 2;
    return SAR_PHANTOM_MAX_PER_DIR;
}

static DWORDLONG SarPhantomSyntheticRef(ULONG dir_hash, ULONG index)
{
    return SAR_PHANTOM_SYNTH_REF_MASK |
           ((DWORDLONG)(dir_hash & 0xFFFF) << 32) |
           ((DWORDLONG)(index & 0xFFFF));
}

static SAR_PHANTOM_TRUST SarPhantomProcessTrust(_In_ HANDLE ProcessId)
{
    PSAR_IDENTITY_ENTRY entry;
    SAR_PHANTOM_TRUST trust = SAR_PHANTOM_UNTRUSTED;

    if (g_sar_state == NULL)
        return SAR_PHANTOM_UNTRUSTED;

    FltAcquirePushLockShared(&g_sar_state->identity_lock);
    {
        ULONG bucket = ((ULONG_PTR)ProcessId ^ ((ULONG_PTR)ProcessId >> 13)) * 0x9E3779B1u;
        bucket = bucket % g_sar_state->identity_bucket_count;
        PLIST_ENTRY head = &g_sar_state->identity_buckets[bucket];
        for (PLIST_ENTRY cur = head->Flink; cur != head; cur = cur->Flink) {
            entry = CONTAINING_RECORD(cur, SAR_IDENTITY_ENTRY, link);
            if (entry->process_id == ProcessId) {
                trust = entry->phantom_trust;
                break;
            }
        }
    }
    FltReleasePushLock(&g_sar_state->identity_lock);
    return trust;
}

static BOOLEAN SarPhantomIsTrusted(_In_ HANDLE ProcessId)
{
    return SarPhantomProcessTrust(ProcessId) == SAR_PHANTOM_TRUSTED;
}

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarPhantomIsTrustedProcess(_In_ HANDLE ProcessId)
{
    return SarPhantomIsTrusted(ProcessId);
}

static HANDLE SarPhantomCurrentPid(PFLT_CALLBACK_DATA Data)
{
    PETHREAD thread = Data->Thread;
    PEPROCESS proc;
    if (thread == NULL)
        thread = PsGetCurrentThread();
    proc = IoThreadToProcess(thread);
    if (proc == NULL)
        proc = PsGetCurrentProcess();
    return PsGetProcessId(proc);
}

static VOID SarPhantomFillEntry(_Out_writes_bytes_(entry_size) PVOID Entry,
                                _In_ const SAR_DIR_OFFSETS *off,
                                _In_ ULONG entry_size,
                                _In_ const WCHAR *Name,
                                _In_ USHORT NameChars,
                                _In_ LARGE_INTEGER CreationTime,
                                _In_ LARGE_INTEGER FileSize,
                                _In_ DWORDLONG SyntheticRef)
{
    ULONG name_bytes = NameChars * sizeof(WCHAR);

    RtlZeroMemory(Entry, entry_size);

    *(ULONG *)((UCHAR *)Entry + off->next_entry) = 0;

    if (off->creation_time != (ULONG)-1) {
        *(LARGE_INTEGER *)((UCHAR *)Entry + off->creation_time) = CreationTime;
        *(LARGE_INTEGER *)((UCHAR *)Entry + off->last_access)   = CreationTime;
        *(LARGE_INTEGER *)((UCHAR *)Entry + off->last_write)    = CreationTime;
        *(LARGE_INTEGER *)((UCHAR *)Entry + off->change_time)   = CreationTime;
        *(LARGE_INTEGER *)((UCHAR *)Entry + off->end_of_file)   = *(LARGE_INTEGER *)&FileSize;

        LARGE_INTEGER alloc;
        alloc.QuadPart = (FileSize.QuadPart + 4095) & ~4095LL;
        *(LARGE_INTEGER *)((UCHAR *)Entry + off->alloc_size) = alloc;

        *(ULONG *)((UCHAR *)Entry + off->attrs) = FILE_ATTRIBUTE_ARCHIVE;
    }

    if (off->name_length != (ULONG)-1)
        *(ULONG *)((UCHAR *)Entry + off->name_length) = name_bytes;

    if (off->name_start != (ULONG)-1)
        RtlCopyMemory((UCHAR *)Entry + off->name_start, Name, name_bytes);

    if (off->ea_size >= 0)
        *(ULONG *)((UCHAR *)Entry + off->ea_size) = 0;

    if (off->short_name_length >= 0)
        *(UCHAR *)((UCHAR *)Entry + off->short_name_length) = 0;

    if (off->file_id >= 0)
        *(DWORDLONG *)((UCHAR *)Entry + off->file_id) = SyntheticRef;
}

static ULONG SarPhantomCountRealEntries(_In_ PVOID Buffer, _In_ ULONG Length,
                                        _In_ const SAR_DIR_OFFSETS *off,
                                        _Out_ PVOID *LastEntry)
{
    ULONG count = 0;
    UCHAR *cur = (UCHAR *)Buffer;
    UCHAR *end = (UCHAR *)Buffer + Length;
    *LastEntry = NULL;

    while (cur < end) {
        ULONG next = *(ULONG *)(cur + off->next_entry);
        count++;
        *LastEntry = cur;
        if (next == 0)
            break;
        cur += next;
    }
    return count;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarPhantomCreate(_In_ PFLT_FILTER Filter, _Outptr_ PSAR_PHANTOM *Phantom)
{
    PSAR_PHANTOM ph;
    UNICODE_STRING name;

    *Phantom = NULL;

    ph = (PSAR_PHANTOM)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(SAR_PHANTOM),
                                       SAR_POOL_TAG_PHANTOM);
    if (ph == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    ph->filter = Filter;
    ph->active = FALSE;
    ph->image_notify_registered = FALSE;
    RtlZeroMemory(ph->volume_secret, SAR_PHANTOM_SECRET_BYTES);

    RtlInitUnicodeString(&name, L"IoReplaceFileObjectName");
    g_sar_io_replace_name = (PIO_REPLACE_FILE_OBJECT_NAME_FN)MmGetSystemRoutineAddress(&name);

    if (g_sar_io_replace_name == NULL) {
        ExFreePoolWithTag(ph, SAR_POOL_TAG_PHANTOM);
        return STATUS_NOT_SUPPORTED;
    }

    *Phantom = ph;
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarPhantomActivate(_Inout_ PSAR_PHANTOM Phantom, _In_ const UCHAR MacKey[32])
{
    SarPhantomDeriveSecret(MacKey, Phantom->volume_secret);
    SarStoreEnsureDir(SAR_PHANTOM_DIR, SAR_POOL_TAG_PHANTOM);
    Phantom->active = TRUE;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarPhantomDestroy(_Inout_ PSAR_PHANTOM Phantom)
{
    if (Phantom == NULL)
        return;
    Phantom->active = FALSE;
    sar_secure_zero(Phantom->volume_secret, SAR_PHANTOM_SECRET_BYTES);
    ExFreePoolWithTag(Phantom, SAR_POOL_TAG_PHANTOM);
}

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarPhantomActive(_In_opt_ PSAR_PHANTOM Phantom)
{
    if (Phantom == NULL) return FALSE;
    return Phantom->active;
}

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarPhantomIsPhantomPath(_In_ PCUNICODE_STRING Path)
{
    static const WCHAR marker[] = L"\\phantom\\";
    USHORT nchars = (USHORT)(Path->Length / sizeof(WCHAR));
    USHORT mlen = (USHORT)(RTL_NUMBER_OF(marker) - 1);

    if (nchars < mlen)
        return FALSE;

    for (USHORT i = 0; (USHORT)(i + mlen) <= nchars; i++) {
        BOOLEAN match = TRUE;
        for (USHORT j = 0; j < mlen; j++) {
            WCHAR a = Path->Buffer[i + j];
            WCHAR b = marker[j];
            if (a >= L'A' && a <= L'Z') a = (WCHAR)(a + 32);
            if (b >= L'A' && b <= L'Z') b = (WCHAR)(b + 32);
            if (a != b) { match = FALSE; break; }
        }
        if (match) return TRUE;
    }
    return FALSE;
}

static BOOLEAN SarPhantomMatchName(_In_ PCUNICODE_STRING FileName,
                                   _In_ PCUNICODE_STRING DirPath,
                                   _In_ PSAR_PHANTOM Phantom,
                                   _Out_ PULONG MatchIndex)
{
    ULONG max_phantoms = SAR_PHANTOM_MAX_PER_DIR;
    WCHAR gen_name[SAR_PHANTOM_NAME_CHARS];
    USHORT gen_chars;
    ULONG i;

    *MatchIndex = (ULONG)-1;

    for (i = 0; i < max_phantoms; i++) {
        SarPhantomNameForIndex(Phantom->volume_secret, DirPath, i, gen_name, &gen_chars);
        if (FileName->Length == gen_chars * sizeof(WCHAR)) {
            if (RtlCompareMemory(FileName->Buffer, gen_name, gen_chars * sizeof(WCHAR))
                == gen_chars * sizeof(WCHAR)) {
                *MatchIndex = i;
                return TRUE;
            }
        }
    }
    return FALSE;
}

static VOID SarPhantomBuildBackingPath(_In_ ULONG PhantomIndex,
                                       _In_ PCUNICODE_STRING DirPath,
                                       _Out_writes_(260) WCHAR *BackingPath,
                                       _Out_ PUSHORT BackingChars)
{
    UINT8 hash_msg[524];
    UINT8 hash[32];
    ULONG dir_bytes = DirPath->Length;
    static const WCHAR hex[] = L"0123456789abcdef";
    USHORT pos = 0;

    static const WCHAR prefix[] = L"\\SystemRoot\\System32\\drivers\\SemanticsAr\\phantom\\";
    USHORT prefix_chars = (USHORT)(RTL_NUMBER_OF(prefix) - 1);
    RtlCopyMemory(BackingPath, prefix, prefix_chars * sizeof(WCHAR));
    pos = prefix_chars;

    if (dir_bytes > 520) dir_bytes = 520;
    RtlCopyMemory(hash_msg, DirPath->Buffer, dir_bytes);
    hash_msg[dir_bytes]     = (UINT8)(PhantomIndex & 0xFF);
    hash_msg[dir_bytes + 1] = (UINT8)((PhantomIndex >> 8) & 0xFF);
    hash_msg[dir_bytes + 2] = (UINT8)((PhantomIndex >> 16) & 0xFF);
    hash_msg[dir_bytes + 3] = (UINT8)((PhantomIndex >> 24) & 0xFF);

    sar_hmac_sha256(g_sar.phantom->volume_secret, 32, hash_msg, dir_bytes + 4, hash);

    for (ULONG i = 0; i < 16 && pos < 258; i++) {
        BackingPath[pos++] = hex[(hash[i] >> 4) & 0xF];
        BackingPath[pos++] = hex[hash[i] & 0xF];
    }
    BackingPath[pos] = L'\0';
    *BackingChars = pos;
}

FLT_PREOP_CALLBACK_STATUS
SarPhantomPreCreate(_Inout_ PFLT_CALLBACK_DATA Data,
                    _In_ PCFLT_RELATED_OBJECTS FltObjects,
                    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext)
{
    PFILE_OBJECT fileObj;
    HANDLE pid;
    PFLT_FILE_NAME_INFORMATION nameInfo = NULL;
    ULONG matchIndex;
    WCHAR backingPath[260];
    USHORT backingChars;

    UNREFERENCED_PARAMETER(CompletionContext);

    if (!SarPhantomActive(g_sar.phantom))
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;

    fileObj = Data->Iopb->TargetFileObject;
    if (fileObj == NULL)
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;

    pid = SarPhantomCurrentPid(Data);

    if (fileObj->FileName.Length == 0 && fileObj->RelatedFileObject == NULL) {
        if (!SarPhantomIsTrusted(pid)) {
            Data->IoStatus.Status = STATUS_ACCESS_DENIED;
            Data->IoStatus.Information = 0;
            return FLT_PREOP_COMPLETE;
        }
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;
    }

    if (FlagOn(Data->Iopb->OperationFlags, SL_OPEN_TARGET_DIRECTORY))
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;

    if (!NT_SUCCESS(FltGetFileNameInformation(Data,
            FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &nameInfo)))
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;

    if (!NT_SUCCESS(FltParseFileNameInformation(nameInfo))) {
        FltReleaseFileNameInformation(nameInfo);
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;
    }

    if (!SarPhantomMatchName(&nameInfo->FinalComponent, &nameInfo->ParentDir,
                             g_sar.phantom, &matchIndex)) {
        FltReleaseFileNameInformation(nameInfo);
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;
    }

    if (SarPhantomIsTrusted(pid)) {
        FltReleaseFileNameInformation(nameInfo);
        Data->IoStatus.Status = STATUS_OBJECT_NAME_NOT_FOUND;
        Data->IoStatus.Information = 0;
        return FLT_PREOP_COMPLETE;
    }

    SarPhantomBuildBackingPath(matchIndex, &nameInfo->ParentDir, backingPath, &backingChars);
    FltReleaseFileNameInformation(nameInfo);

    if (g_sar_io_replace_name == NULL) {
        Data->IoStatus.Status = STATUS_OBJECT_NAME_NOT_FOUND;
        Data->IoStatus.Information = 0;
        return FLT_PREOP_COMPLETE;
    }

    SarPhantomRecordEvidence(pid);

    g_sar_io_replace_name(fileObj, backingPath, backingChars * sizeof(WCHAR));
    Data->IoStatus.Status = STATUS_REPARSE;
    Data->IoStatus.Information = IO_REPARSE;
    FltSetCallbackDataDirty(Data);
    return FLT_PREOP_COMPLETE;
}

FLT_PREOP_CALLBACK_STATUS
SarPhantomPreDirControl(_Inout_ PFLT_CALLBACK_DATA Data,
                        _In_ PCFLT_RELATED_OBJECTS FltObjects,
                        _Flt_CompletionContext_Outptr_ PVOID *CompletionContext)
{
    PSAR_PHANTOM_DIR_SWAP_CTX swapCtx;
    ULONG origLen;
    ULONG swapLen;
    PVOID swapBuf;
    PMDL swapMdl;
    HANDLE pid;
    FILE_INFORMATION_CLASS infoClass;

    *CompletionContext = NULL;

    if (!SarPhantomActive(g_sar.phantom))
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;

    if (Data->Iopb == NULL)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    if (Data->Iopb->MinorFunction != IRP_MN_QUERY_DIRECTORY)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    infoClass = Data->Iopb->Parameters.DirectoryControl.QueryDirectory.FileInformationClass;
    if (SarPhantomDirOffsets(infoClass) == NULL)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    pid = SarPhantomCurrentPid(Data);
    if (SarPhantomIsTrusted(pid))
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    origLen = Data->Iopb->Parameters.DirectoryControl.QueryDirectory.Length;
    swapLen = origLen + SAR_PHANTOM_SWAP_EXTRA;

    swapBuf = ExAllocatePool2(POOL_FLAG_NON_PAGED, swapLen, SAR_POOL_TAG_PHANTOM);
    if (swapBuf == NULL)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    swapMdl = IoAllocateMdl(swapBuf, swapLen, FALSE, FALSE, NULL);
    if (swapMdl == NULL) {
        ExFreePoolWithTag(swapBuf, SAR_POOL_TAG_PHANTOM);
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }
    MmBuildMdlForNonPagedPool(swapMdl);

    swapCtx = (PSAR_PHANTOM_DIR_SWAP_CTX)ExAllocatePool2(POOL_FLAG_NON_PAGED,
              sizeof(SAR_PHANTOM_DIR_SWAP_CTX), SAR_POOL_TAG_PHANTOM);
    if (swapCtx == NULL) {
        IoFreeMdl(swapMdl);
        ExFreePoolWithTag(swapBuf, SAR_POOL_TAG_PHANTOM);
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    swapCtx->original_buffer = Data->Iopb->Parameters.DirectoryControl.QueryDirectory.DirectoryBuffer;
    swapCtx->original_mdl    = Data->Iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress;
    swapCtx->swap_buffer     = swapBuf;
    swapCtx->swap_mdl        = swapMdl;
    swapCtx->swap_length     = swapLen;
    swapCtx->info_class      = infoClass;

    Data->Iopb->Parameters.DirectoryControl.QueryDirectory.DirectoryBuffer = swapBuf;
    Data->Iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress      = swapMdl;
    Data->Iopb->Parameters.DirectoryControl.QueryDirectory.Length           = swapLen;
    FltSetCallbackDataDirty(Data);

    *CompletionContext = swapCtx;
    return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}

FLT_POSTOP_CALLBACK_STATUS
SarPhantomPostDirControl(_Inout_ PFLT_CALLBACK_DATA Data,
                         _In_ PCFLT_RELATED_OBJECTS FltObjects,
                         _In_opt_ PVOID CompletionContext,
                         _In_ FLT_POST_OPERATION_FLAGS Flags)
{
    PSAR_PHANTOM_DIR_SWAP_CTX swapCtx = (PSAR_PHANTOM_DIR_SWAP_CTX)CompletionContext;
    const SAR_DIR_OFFSETS *off;
    ULONG returned;
    PVOID lastEntry;
    ULONG realCount;
    ULONG phantomCount;
    ULONG i;
    ULONG usedBytes;
    PFLT_FILE_NAME_INFORMATION nameInfo = NULL;
    WCHAR phantomName[SAR_PHANTOM_NAME_CHARS];
    USHORT phantomChars;
    LARGE_INTEGER now;
    LARGE_INTEGER phantomSize;

    if (swapCtx == NULL)
        return FLT_POSTOP_FINISHED_PROCESSING;

    if (FlagOn(Flags, FLTFL_POST_OPERATION_DRAINING))
        goto cleanup;

    off = SarPhantomDirOffsets(swapCtx->info_class);
    if (off == NULL)
        goto cleanup;

    if (!NT_SUCCESS(Data->IoStatus.Status) || Data->IoStatus.Information == 0) {
        if (swapCtx->original_buffer != NULL) {
            Data->Iopb->Parameters.DirectoryControl.QueryDirectory.DirectoryBuffer = swapCtx->original_buffer;
            Data->Iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress      = swapCtx->original_mdl;
            FltSetCallbackDataDirty(Data);
        }
        goto cleanup;
    }

    returned = (ULONG)Data->IoStatus.Information;
    realCount = SarPhantomCountRealEntries(swapCtx->swap_buffer, returned, off, &lastEntry);
    phantomCount = SarPhantomCountForDir(realCount);

    if (phantomCount == 0 || lastEntry == NULL) {
        if (returned <= (swapCtx->swap_length - SAR_PHANTOM_SWAP_EXTRA) && swapCtx->original_buffer) {
            __try {
                RtlCopyMemory(swapCtx->original_buffer, swapCtx->swap_buffer, returned);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                /* best effort */
            }
            Data->Iopb->Parameters.DirectoryControl.QueryDirectory.DirectoryBuffer = swapCtx->original_buffer;
            Data->Iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress      = swapCtx->original_mdl;
            FltSetCallbackDataDirty(Data);
        }
        goto cleanup;
    }

    if (!NT_SUCCESS(FltGetFileNameInformation(Data,
            FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &nameInfo))) {
        if (returned <= (swapCtx->swap_length - SAR_PHANTOM_SWAP_EXTRA) && swapCtx->original_buffer) {
            __try {
                RtlCopyMemory(swapCtx->original_buffer, swapCtx->swap_buffer, returned);
            } __except (EXCEPTION_EXECUTE_HANDLER) { }
            Data->Iopb->Parameters.DirectoryControl.QueryDirectory.DirectoryBuffer = swapCtx->original_buffer;
            Data->Iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress      = swapCtx->original_mdl;
            FltSetCallbackDataDirty(Data);
        }
        goto cleanup;
    }
    FltParseFileNameInformation(nameInfo);

    KeQuerySystemTimePrecise(&now);
    phantomSize.QuadPart = 8192;
    usedBytes = returned;

    for (i = 0; i < phantomCount; i++) {
        ULONG entrySize;
        ULONG nameBytes;
        ULONG dir_hash;
        DWORDLONG synthRef;

        SarPhantomNameForIndex(g_sar.phantom->volume_secret, &nameInfo->Name,
                               i, phantomName, &phantomChars);
        nameBytes = phantomChars * sizeof(WCHAR);
        entrySize = SarPhantomEntrySize(off, nameBytes);

        if (usedBytes + entrySize > swapCtx->swap_length)
            break;

        dir_hash = (ULONG)(nameInfo->Name.Length / sizeof(WCHAR));
        synthRef = SarPhantomSyntheticRef(dir_hash, i);

        *(ULONG *)((UCHAR *)lastEntry + off->next_entry) =
            (ULONG)((UCHAR *)swapCtx->swap_buffer + usedBytes - (UCHAR *)lastEntry);

        SarPhantomFillEntry((UCHAR *)swapCtx->swap_buffer + usedBytes, off,
                            entrySize, phantomName, phantomChars, now, phantomSize, synthRef);

        lastEntry = (UCHAR *)swapCtx->swap_buffer + usedBytes;
        usedBytes += entrySize;
    }

    FltReleaseFileNameInformation(nameInfo);

    if (swapCtx->original_buffer != NULL) {
        ULONG copyLen = usedBytes;
        ULONG origCap = swapCtx->swap_length - SAR_PHANTOM_SWAP_EXTRA;
        if (copyLen > origCap)
            copyLen = origCap;
        __try {
            RtlCopyMemory(swapCtx->original_buffer, swapCtx->swap_buffer, copyLen);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            copyLen = returned;
            __try {
                RtlCopyMemory(swapCtx->original_buffer, swapCtx->swap_buffer, copyLen);
            } __except (EXCEPTION_EXECUTE_HANDLER) { }
        }
        Data->IoStatus.Information = copyLen;
        Data->Iopb->Parameters.DirectoryControl.QueryDirectory.DirectoryBuffer = swapCtx->original_buffer;
        Data->Iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress      = swapCtx->original_mdl;
        FltSetCallbackDataDirty(Data);
    } else {
        Data->IoStatus.Information = usedBytes;
    }

cleanup:
    if (swapCtx != NULL) {
        if (swapCtx->swap_mdl != NULL)
            IoFreeMdl(swapCtx->swap_mdl);
        if (swapCtx->swap_buffer != NULL)
            ExFreePoolWithTag(swapCtx->swap_buffer, SAR_POOL_TAG_PHANTOM);
        ExFreePoolWithTag(swapCtx, SAR_POOL_TAG_PHANTOM);
    }
    return FLT_POSTOP_FINISHED_PROCESSING;
}

FLT_POSTOP_CALLBACK_STATUS
SarPhantomPostQueryInfo(_Inout_ PFLT_CALLBACK_DATA Data,
                        _In_ PCFLT_RELATED_OBJECTS FltObjects,
                        _In_opt_ PVOID CompletionContext,
                        _In_ FLT_POST_OPERATION_FLAGS Flags)
{
    PFLT_FILE_NAME_INFORMATION nameInfo = NULL;
    FILE_INFORMATION_CLASS infoClass;
    HANDLE pid;

    UNREFERENCED_PARAMETER(CompletionContext);

    if (FlagOn(Flags, FLTFL_POST_OPERATION_DRAINING))
        return FLT_POSTOP_FINISHED_PROCESSING;
    if (!SarPhantomActive(g_sar.phantom))
        return FLT_POSTOP_FINISHED_PROCESSING;
    if (!NT_SUCCESS(Data->IoStatus.Status))
        return FLT_POSTOP_FINISHED_PROCESSING;
    if (Data->Iopb == NULL)
        return FLT_POSTOP_FINISHED_PROCESSING;

    pid = SarPhantomCurrentPid(Data);
    if (SarPhantomIsTrusted(pid))
        return FLT_POSTOP_FINISHED_PROCESSING;

    if (!NT_SUCCESS(FltGetFileNameInformation(Data,
            FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &nameInfo)))
        return FLT_POSTOP_FINISHED_PROCESSING;

    if (!NT_SUCCESS(FltParseFileNameInformation(nameInfo)) ||
        !SarPhantomIsPhantomPath(&nameInfo->Name)) {
        FltReleaseFileNameInformation(nameInfo);
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    infoClass = Data->Iopb->Parameters.QueryFileInformation.FileInformationClass;

    if (infoClass == FileNameInformation || infoClass == FileNormalizedNameInformation) {
        PFILE_NAME_INFORMATION info =
            (PFILE_NAME_INFORMATION)Data->Iopb->Parameters.QueryFileInformation.InfoBuffer;
        ULONG matchIndex;
        UNICODE_STRING parentDir;
        WCHAR phantomName[SAR_PHANTOM_NAME_CHARS];
        USHORT phantomChars;

        parentDir.Buffer = nameInfo->ParentDir.Buffer;
        parentDir.Length = nameInfo->ParentDir.Length;
        parentDir.MaximumLength = nameInfo->ParentDir.MaximumLength;

        if (SarPhantomMatchName(&nameInfo->FinalComponent, &parentDir,
                                g_sar.phantom, &matchIndex)) {
            SarPhantomNameForIndex(g_sar.phantom->volume_secret, &parentDir,
                                  matchIndex, phantomName, &phantomChars);

            ULONG needed = parentDir.Length + phantomChars * sizeof(WCHAR);
            ULONG bufCap = Data->Iopb->Parameters.QueryFileInformation.Length
                           - FIELD_OFFSET(FILE_NAME_INFORMATION, FileName);
            if (needed <= bufCap) {
                RtlCopyMemory(info->FileName, parentDir.Buffer, parentDir.Length);
                RtlCopyMemory((UCHAR *)info->FileName + parentDir.Length,
                              phantomName, phantomChars * sizeof(WCHAR));
                info->FileNameLength = needed;
                Data->IoStatus.Information = FIELD_OFFSET(FILE_NAME_INFORMATION, FileName) + needed;
                FltSetCallbackDataDirty(Data);
            }
        }
    } else if (infoClass == FileInternalInformation) {
        PFILE_INTERNAL_INFORMATION info =
            (PFILE_INTERNAL_INFORMATION)Data->Iopb->Parameters.QueryFileInformation.InfoBuffer;
        ULONG matchIndex;
        if (SarPhantomMatchName(&nameInfo->FinalComponent, &nameInfo->ParentDir,
                                g_sar.phantom, &matchIndex)) {
            ULONG dir_hash = (ULONG)(nameInfo->ParentDir.Length / sizeof(WCHAR));
            info->IndexNumber.QuadPart = (LONGLONG)SarPhantomSyntheticRef(dir_hash, matchIndex);
            FltSetCallbackDataDirty(Data);
        }
    }

    FltReleaseFileNameInformation(nameInfo);
    return FLT_POSTOP_FINISHED_PROCESSING;
}

FLT_POSTOP_CALLBACK_STATUS
SarPhantomPostFsControl(_Inout_ PFLT_CALLBACK_DATA Data,
                        _In_ PCFLT_RELATED_OBJECTS FltObjects,
                        _In_opt_ PVOID CompletionContext,
                        _In_ FLT_POST_OPERATION_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(CompletionContext);

    if (FlagOn(Flags, FLTFL_POST_OPERATION_DRAINING))
        return FLT_POSTOP_FINISHED_PROCESSING;
    if (!SarPhantomActive(g_sar.phantom))
        return FLT_POSTOP_FINISHED_PROCESSING;
    if (!NT_SUCCESS(Data->IoStatus.Status))
        return FLT_POSTOP_FINISHED_PROCESSING;
    if (Data->Iopb == NULL)
        return FLT_POSTOP_FINISHED_PROCESSING;

    return FLT_POSTOP_FINISHED_PROCESSING;
}

_IRQL_requires_max_(APC_LEVEL)
VOID SarPhantomRecordEvidence(_In_ HANDLE ProcessId)
{
    if (g_sar_state == NULL)
        return;

    FltAcquirePushLockShared(&g_sar_state->identity_lock);
    {
        ULONG bucket = ((ULONG_PTR)ProcessId ^ ((ULONG_PTR)ProcessId >> 13)) * 0x9E3779B1u;
        bucket = bucket % g_sar_state->identity_bucket_count;
        PLIST_ENTRY head = &g_sar_state->identity_buckets[bucket];
        for (PLIST_ENTRY cur = head->Flink; cur != head; cur = cur->Flink) {
            PSAR_IDENTITY_ENTRY entry = CONTAINING_RECORD(cur, SAR_IDENTITY_ENTRY, link);
            if (entry->process_id == ProcessId) {
                InterlockedIncrement(&entry->phantom_evidence);
                break;
            }
        }
    }
    FltReleasePushLock(&g_sar_state->identity_lock);
}

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarPhantomIsConvicted(_In_ HANDLE ProcessId)
{
    if (g_sar_state == NULL)
        return FALSE;

    LONG evidence = 0;
    FltAcquirePushLockShared(&g_sar_state->identity_lock);
    {
        ULONG bucket = ((ULONG_PTR)ProcessId ^ ((ULONG_PTR)ProcessId >> 13)) * 0x9E3779B1u;
        bucket = bucket % g_sar_state->identity_bucket_count;
        PLIST_ENTRY head = &g_sar_state->identity_buckets[bucket];
        for (PLIST_ENTRY cur = head->Flink; cur != head; cur = cur->Flink) {
            PSAR_IDENTITY_ENTRY entry = CONTAINING_RECORD(cur, SAR_IDENTITY_ENTRY, link);
            if (entry->process_id == ProcessId) {
                evidence = entry->phantom_evidence;
                break;
            }
        }
    }
    FltReleasePushLock(&g_sar_state->identity_lock);
    return evidence >= SAR_PHANTOM_K_THRESHOLD;
}

static VOID SarPhantomLoadImageNotify(_In_opt_ PUNICODE_STRING FullImageName,
                                      _In_ HANDLE ProcessId,
                                      _In_ PIMAGE_INFO ImageInfo)
{
    UNREFERENCED_PARAMETER(FullImageName);

    if (g_sar_state == NULL)
        return;
    if (ImageInfo->SystemModeImage)
        return;
    if (ProcessId == NULL)
        return;

    ULONG sigLevel = (ImageInfo->Properties >> 12) & 0xF;
    if (sigLevel >= 4)
        return;

    FltAcquirePushLockExclusive(&g_sar_state->identity_lock);
    {
        ULONG bucket = ((ULONG_PTR)ProcessId ^ ((ULONG_PTR)ProcessId >> 13)) * 0x9E3779B1u;
        bucket = bucket % g_sar_state->identity_bucket_count;
        PLIST_ENTRY head = &g_sar_state->identity_buckets[bucket];
        for (PLIST_ENTRY cur = head->Flink; cur != head; cur = cur->Flink) {
            PSAR_IDENTITY_ENTRY entry = CONTAINING_RECORD(cur, SAR_IDENTITY_ENTRY, link);
            if (entry->process_id == ProcessId) {
                if (entry->phantom_trust == SAR_PHANTOM_TRUSTED)
                    entry->phantom_trust = SAR_PHANTOM_TAINTED;
                break;
            }
        }
    }
    FltReleasePushLock(&g_sar_state->identity_lock);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarPhantomImageNotifyRegister(VOID)
{
    if (g_sar.phantom == NULL)
        return;
    if (NT_SUCCESS(PsSetLoadImageNotifyRoutine(SarPhantomLoadImageNotify)))
        g_sar.phantom->image_notify_registered = TRUE;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarPhantomImageNotifyUnregister(VOID)
{
    if (g_sar.phantom == NULL)
        return;
    if (g_sar.phantom->image_notify_registered) {
        PsRemoveLoadImageNotifyRoutine(SarPhantomLoadImageNotify);
        g_sar.phantom->image_notify_registered = FALSE;
    }
}
