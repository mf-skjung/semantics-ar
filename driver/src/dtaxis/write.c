#include "driver_internal.h"
#include "preserve.h"
#include "tgate.h"
#include "emit.h"

static PUCHAR get_write_buffer(PFLT_CALLBACK_DATA Data, PULONG Length)
{
    PMDL mdl;
    *Length = Data->Iopb->Parameters.Write.Length;
    if (*Length == 0)
        return NULL;
    mdl = Data->Iopb->Parameters.Write.MdlAddress;
    if (mdl)
        return (PUCHAR)MmGetSystemAddressForMdlSafe(mdl, NormalPagePriority | MdlMappingNoExecute);
    if (!Data->Iopb->Parameters.Write.WriteBuffer)
        return NULL;
    if (!NT_SUCCESS(FltLockUserBuffer(Data)))
        return NULL;
    mdl = Data->Iopb->Parameters.Write.MdlAddress;
    if (!mdl)
        return NULL;
    return (PUCHAR)MmGetSystemAddressForMdlSafe(mdl, NormalPagePriority | MdlMappingNoExecute);
}

static BOOLEAN is_efs_encrypted(PCFLT_RELATED_OBJECTS FltObjects)
{
    FILE_BASIC_INFORMATION bi;
    NTSTATUS s = FltQueryInformationFile(FltObjects->Instance, FltObjects->FileObject,
        &bi, sizeof(bi), FileBasicInformation, NULL);
    return NT_SUCCESS(s) && BooleanFlagOn(bi.FileAttributes, FILE_ATTRIBUTE_ENCRYPTED);
}

FLT_PREOP_CALLBACK_STATUS semantics_ar_pre_write(
    PFLT_CALLBACK_DATA Data,
    PCFLT_RELATED_OBJECTS FltObjects,
    PVOID *CompletionContext)
{
    *CompletionContext = NULL;
    if (!FLT_IS_IRP_OPERATION(Data))
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    if (FlagOn(Data->Iopb->IrpFlags, IRP_PAGING_IO))
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    ULONG pid = FltGetRequestorProcessId(Data);
    if (pid == 0 || pid == 4)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    if (semantics_ar_globals.ServiceProcessId != 0 &&
        pid == semantics_ar_globals.ServiceProcessId)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    if (semantics_ar_globals.ConfirmedActive && semantics_ar_is_confirmed_process(pid)) {
        Data->IoStatus.Status = STATUS_ACCESS_DENIED;
        Data->IoStatus.Information = 0;
        return FLT_PREOP_COMPLETE;
    }

    PUCHAR writeBuf;
    ULONG writeLen;
    writeBuf = get_write_buffer(Data, &writeLen);
    if (!writeBuf || writeLen < SEMANTICS_AR_CHUNK_SIZE)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    if (is_efs_encrypted(FltObjects))
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    LARGE_INTEGER offset = Data->Iopb->Parameters.Write.ByteOffset;
    FILE_STANDARD_INFORMATION fi;
    if (!NT_SUCCESS(FltQueryInformationFile(FltObjects->Instance, FltObjects->FileObject,
            &fi, sizeof(fi), FileStandardInformation, NULL)))
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    if (offset.QuadPart >= fi.EndOfFile.QuadPart)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    if (fi.EndOfFile.QuadPart - offset.QuadPart < SEMANTICS_AR_CHUNK_SIZE)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    PUCHAR oldChunk = (PUCHAR)ExAllocatePoolZero(PagedPool,
        SEMANTICS_AR_CHUNK_SIZE, SEMANTICS_AR_POOL_TAG_PG);
    if (!oldChunk)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    ULONG bytesRead = 0;
    semantics_ar_result_t rd = semantics_ar_capture_read(FltObjects->Instance,
        FltObjects->FileObject, offset, SEMANTICS_AR_CHUNK_SIZE, oldChunk, &bytesRead);
    if (rd != SEMANTICS_AR_OK || bytesRead < SEMANTICS_AR_CHUNK_SIZE) {
        ExFreePoolWithTag(oldChunk, SEMANTICS_AR_POOL_TAG_PG);
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    semantics_ar_tgate_verdict_t verdict = semantics_ar_tgate_classify(
        oldChunk, SEMANTICS_AR_CHUNK_SIZE, writeBuf, SEMANTICS_AR_CHUNK_SIZE);
    if (verdict != SEMANTICS_AR_TGATE_PRESERVE) {
        ExFreePoolWithTag(oldChunk, SEMANTICS_AR_POOL_TAG_PG);
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    ULONG preserveLen = writeLen;
    if ((LONGLONG)(offset.QuadPart + preserveLen) > fi.EndOfFile.QuadPart)
        preserveLen = (ULONG)(fi.EndOfFile.QuadPart - offset.QuadPart);

    semantics_ar_result_t pres = semantics_ar_preserve_range(
        Data, FltObjects->Instance, FltObjects->FileObject, NULL,
        offset, preserveLen, fi.EndOfFile.QuadPart,
        SEMANTICS_AR_JOURNAL_OVERWRITE, pid);
    if (pres != SEMANTICS_AR_OK) {
        ExFreePoolWithTag(oldChunk, SEMANTICS_AR_POOL_TAG_PG);
        Data->IoStatus.Status = STATUS_ACCESS_DENIED;
        Data->IoStatus.Information = 0;
        return FLT_PREOP_COMPLETE;
    }

    if (semantics_ar_globals.ProtectionActive)
        semantics_ar_emit_chain_candidate(Data, pid, offset, writeLen, oldChunk, writeBuf);

    ExFreePoolWithTag(oldChunk, SEMANTICS_AR_POOL_TAG_PG);
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}