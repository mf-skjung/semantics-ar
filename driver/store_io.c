#include "store_io.h"

#include <ntifs.h>

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS SarStoreBuildSecurity(_In_ ULONG SecTag, _Outptr_ PSECURITY_DESCRIPTOR *Sd,
                                      _Outptr_ PACL *Dacl)
{
    NTSTATUS status;
    PSID system = SeExports->SeLocalSystemSid;
    PSID admins = SeExports->SeAliasAdminsSid;
    ULONG acl_size;
    PACL dacl;
    PSECURITY_DESCRIPTOR sd;

    *Sd = NULL;
    *Dacl = NULL;

    acl_size = (ULONG)(sizeof(ACL) + 2 * (sizeof(ACCESS_ALLOWED_ACE) - sizeof(ULONG)) +
                       RtlLengthSid(system) + RtlLengthSid(admins));

    sd = ExAllocatePool2(POOL_FLAG_PAGED, sizeof(SECURITY_DESCRIPTOR), SecTag);
    if (sd == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    dacl = ExAllocatePool2(POOL_FLAG_PAGED, acl_size, SecTag);
    if (dacl == NULL) {
        ExFreePoolWithTag(sd, SecTag);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = RtlCreateSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION);
    if (NT_SUCCESS(status))
        status = RtlCreateAcl(dacl, acl_size, ACL_REVISION);
    if (NT_SUCCESS(status))
        status = RtlAddAccessAllowedAce(dacl, ACL_REVISION, FILE_ALL_ACCESS, system);
    if (NT_SUCCESS(status))
        status = RtlAddAccessAllowedAce(dacl, ACL_REVISION, FILE_ALL_ACCESS, admins);
    if (NT_SUCCESS(status))
        status = RtlSetDaclSecurityDescriptor(sd, TRUE, dacl, FALSE);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(dacl, SecTag);
        ExFreePoolWithTag(sd, SecTag);
        return status;
    }

    *Sd = sd;
    *Dacl = dacl;
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarStoreEnsureDir(_In_ PCWSTR Dir, _In_ ULONG SecTag)
{
    PSECURITY_DESCRIPTOR sd = NULL;
    PACL dacl = NULL;
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    HANDLE h;
    NTSTATUS status;

    if (!NT_SUCCESS(SarStoreBuildSecurity(SecTag, &sd, &dacl))) {
        sd = NULL;
        dacl = NULL;
    }

    RtlInitUnicodeString(&name, Dir);
    InitializeObjectAttributes(&oa, &name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, sd);

    status = ZwCreateFile(&h, FILE_LIST_DIRECTORY | SYNCHRONIZE, &oa, &iosb, NULL,
                          FILE_ATTRIBUTE_NORMAL, 0, FILE_OPEN_IF,
                          FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    if (NT_SUCCESS(status))
        ZwClose(h);

    if (sd != NULL) {
        ExFreePoolWithTag(dacl, SecTag);
        ExFreePoolWithTag(sd, SecTag);
    }
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarStoreReadAll(_In_ PCWSTR Path, _In_ ULONG BufTag, _In_ ULONG64 MaxBytes,
                         _Outptr_result_maybenull_ PUCHAR *OutBuf, _Out_ SIZE_T *OutLen)
{
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    HANDLE h;
    NTSTATUS status;
    FILE_STANDARD_INFORMATION si;
    PUCHAR buf;
    SIZE_T len;
    LARGE_INTEGER off;

    *OutBuf = NULL;
    *OutLen = 0;

    RtlInitUnicodeString(&name, Path);
    InitializeObjectAttributes(&oa, &name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

    status = ZwCreateFile(&h, FILE_READ_DATA | SYNCHRONIZE, &oa, &iosb, NULL,
                          FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0);
    if (!NT_SUCCESS(status))
        return status;

    status = ZwQueryInformationFile(h, &iosb, &si, sizeof(si), FileStandardInformation);
    if (!NT_SUCCESS(status)) {
        ZwClose(h);
        return status;
    }
    if (si.EndOfFile.QuadPart == 0) {
        ZwClose(h);
        return STATUS_SUCCESS;
    }
    if ((ULONG64)si.EndOfFile.QuadPart > MaxBytes) {
        ZwClose(h);
        return STATUS_FILE_TOO_LARGE;
    }

    len = (SIZE_T)si.EndOfFile.QuadPart;
    buf = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, len, BufTag);
    if (buf == NULL) {
        ZwClose(h);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    off.QuadPart = 0;
    status = ZwReadFile(h, NULL, NULL, NULL, &iosb, buf, (ULONG)len, &off, NULL);
    ZwClose(h);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(buf, BufTag);
        return status;
    }

    *OutBuf = buf;
    *OutLen = iosb.Information;
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS SarStoreWriteRaw(_In_ PCWSTR Path, _In_reads_bytes_(Len) const VOID *Buf,
                                 _In_ ULONG Len)
{
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    HANDLE h;
    NTSTATUS status;

    RtlInitUnicodeString(&name, Path);
    InitializeObjectAttributes(&oa, &name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

    status = ZwCreateFile(&h, FILE_WRITE_DATA | SYNCHRONIZE, &oa, &iosb, NULL,
                          FILE_ATTRIBUTE_NORMAL, 0, FILE_OVERWRITE_IF,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0);
    if (!NT_SUCCESS(status))
        return status;

    status = ZwWriteFile(h, NULL, NULL, NULL, &iosb, (PVOID)(ULONG_PTR)Buf, Len, NULL, NULL);
    if (NT_SUCCESS(status))
        status = ZwFlushBuffersFile(h, &iosb);
    ZwClose(h);
    return status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS SarStoreRename(_In_ PCWSTR Tmp, _In_ PCWSTR Final, _In_ ULONG BufTag)
{
    UNICODE_STRING tmp_name;
    UNICODE_STRING final_name;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    HANDLE h;
    NTSTATUS status;
    PFILE_RENAME_INFORMATION info;
    ULONG info_len;

    RtlInitUnicodeString(&tmp_name, Tmp);
    RtlInitUnicodeString(&final_name, Final);

    InitializeObjectAttributes(&oa, &tmp_name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    status = ZwCreateFile(&h, DELETE | SYNCHRONIZE, &oa, &iosb, NULL, FILE_ATTRIBUTE_NORMAL,
                          0, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
                          NULL, 0);
    if (!NT_SUCCESS(status))
        return status;

    info_len = (ULONG)(FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) + final_name.Length);
    info = (PFILE_RENAME_INFORMATION)ExAllocatePool2(POOL_FLAG_PAGED, info_len, BufTag);
    if (info == NULL) {
        ZwClose(h);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    info->ReplaceIfExists = TRUE;
    info->RootDirectory = NULL;
    info->FileNameLength = final_name.Length;
    RtlCopyMemory(info->FileName, final_name.Buffer, final_name.Length);

    status = ZwSetInformationFile(h, &iosb, info, info_len, FileRenameInformation);

    ExFreePoolWithTag(info, BufTag);
    ZwClose(h);
    return status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarStoreWriteAtomic(_In_ PCWSTR Tmp, _In_ PCWSTR Final,
                             _In_reads_bytes_(Len) const VOID *Buf, _In_ ULONG Len,
                             _In_ ULONG BufTag)
{
    NTSTATUS status = SarStoreWriteRaw(Tmp, Buf, Len);
    if (!NT_SUCCESS(status))
        return status;
    return SarStoreRename(Tmp, Final, BufTag);
}
