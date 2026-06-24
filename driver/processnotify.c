#include "driver.h"
#include "state.h"

extern PSAR_STATE g_sar_state;

typedef NTSTATUS (NTAPI *SAR_PSSET_CREATE_EX2)(PSCREATEPROCESSNOTIFYTYPE, PVOID, BOOLEAN);

static SAR_PSSET_CREATE_EX2 g_sar_pscreate_ex2;
static BOOLEAN g_sar_registered_ex2;
static BOOLEAN g_sar_registered_ex;

_IRQL_requires_max_(PASSIVE_LEVEL)
static VOID SarCaptureCanonicalIdentity(_In_ PPS_CREATE_NOTIFY_INFO CreateInfo,
                                        _Out_ sar_identity_t *Identity,
                                        _Out_ PBOOLEAN Valid)
{
    PFLT_FILE_NAME_INFORMATION name_info = NULL;
    NTSTATUS status;
    USHORT count;
    USHORT i;

    RtlZeroMemory(Identity, sizeof(*Identity));
    *Valid = FALSE;

    if (CreateInfo->FileObject == NULL)
        return;
    if (!CreateInfo->FileOpenNameAvailable)
        return;

    status = FltGetFileNameInformationUnsafe(CreateInfo->FileObject, NULL,
                                             FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT,
                                             &name_info);
    if (!NT_SUCCESS(status))
        return;

    status = FltParseFileNameInformation(name_info);
    if (!NT_SUCCESS(status)) {
        FltReleaseFileNameInformation(name_info);
        return;
    }

    count = (USHORT)(name_info->Name.Length / sizeof(WCHAR));
    if (count >= SEMANTICS_AR_PROTO_PATH_MAX)
        count = SEMANTICS_AR_PROTO_PATH_MAX - 1;
    for (i = 0; i < count; i++)
        Identity->image_path[i] = (uint16_t)name_info->Name.Buffer[i];
    Identity->image_path[count] = 0;
    *Valid = TRUE;

    FltReleaseFileNameInformation(name_info);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static VOID SarCreateProcessNotifyEx(_Inout_ PEPROCESS Process,
                                     _In_ HANDLE ProcessId,
                                     _Inout_opt_ PPS_CREATE_NOTIFY_INFO CreateInfo)
{
    sar_identity_t identity;
    BOOLEAN valid = FALSE;
    BOOLEAN subsystem = FALSE;
    NTSTATUS status;

    if (g_sar_state == NULL)
        return;

    if (CreateInfo == NULL) {
        SarStateIdentityRemove(g_sar_state, ProcessId);
        return;
    }

    if (CreateInfo->FileObject == NULL && CreateInfo->ImageFileName == NULL)
        subsystem = TRUE;

    if (!subsystem)
        SarCaptureCanonicalIdentity(CreateInfo, &identity, &valid);
    else
        RtlZeroMemory(&identity, sizeof(identity));

    ObReferenceObject(Process);
    status = SarStateIdentityInsert(g_sar_state, ProcessId, Process,
                                    valid ? &identity : NULL, valid, subsystem);
    if (!NT_SUCCESS(status))
        ObDereferenceObject(Process);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarProcessNotifyRegister(_Inout_ PSAR_POSTURE Posture)
{
    UNICODE_STRING name;
    NTSTATUS status;

    RtlInitUnicodeString(&name, L"PsSetCreateProcessNotifyRoutineEx2");
    g_sar_pscreate_ex2 = (SAR_PSSET_CREATE_EX2)MmGetSystemRoutineAddress(&name);

    if (g_sar_pscreate_ex2 != NULL) {
        status = g_sar_pscreate_ex2(PsCreateProcessNotifySubsystems,
                                    (PVOID)SarCreateProcessNotifyEx, FALSE);
        if (NT_SUCCESS(status)) {
            g_sar_registered_ex2 = TRUE;
            Posture->process_notify_ex2 = TRUE;
            return STATUS_SUCCESS;
        }
    }

    status = PsSetCreateProcessNotifyRoutineEx(SarCreateProcessNotifyEx, FALSE);
    if (NT_SUCCESS(status)) {
        g_sar_registered_ex = TRUE;
        Posture->process_notify_ex2 = FALSE;
        return STATUS_SUCCESS;
    }

    return status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarProcessNotifyUnregister(VOID)
{
    if (g_sar_registered_ex2 && g_sar_pscreate_ex2 != NULL) {
        (VOID)g_sar_pscreate_ex2(PsCreateProcessNotifySubsystems,
                                 (PVOID)SarCreateProcessNotifyEx, TRUE);
        g_sar_registered_ex2 = FALSE;
    }
    if (g_sar_registered_ex) {
        (VOID)PsSetCreateProcessNotifyRoutineEx(SarCreateProcessNotifyEx, TRUE);
        g_sar_registered_ex = FALSE;
    }
}
