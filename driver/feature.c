#include "feature.h"

#include <ntddk.h>

typedef BOOLEAN (NTAPI *SAR_EX_IS_PROCESSOR_FEATURE_PRESENT)(ULONG);

_IRQL_requires_max_(PASSIVE_LEVEL)
static BOOLEAN SarDetectHvci(VOID)
{
    SYSTEM_CODEINTEGRITY_INFORMATION ci;
    NTSTATUS status;
    ULONG returned = 0;

    RtlZeroMemory(&ci, sizeof(ci));
    ci.Length = sizeof(ci);

    status = ZwQuerySystemInformation(SystemCodeIntegrityInformation, &ci, sizeof(ci), &returned);
    if (!NT_SUCCESS(status))
        return FALSE;

    return BooleanFlagOn(ci.CodeIntegrityOptions, CODEINTEGRITY_OPTION_HVCI_KMCI_ENABLED);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static BOOLEAN SarDetectVbs(VOID)
{
    SYSTEM_ISOLATED_USER_MODE_INFORMATION ium;
    NTSTATUS status;
    ULONG returned = 0;

    RtlZeroMemory(&ium, sizeof(ium));
    status = ZwQuerySystemInformation(SystemIsolatedUserModeInformation, &ium, sizeof(ium), &returned);
    if (!NT_SUCCESS(status) || returned < sizeof(ium))
        return FALSE;

    return ium.SecureKernelRunning ? TRUE : FALSE;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static BOOLEAN SarDetectTpm20(VOID)
{
    UNICODE_STRING name;
    PVOID tbs_register;

    RtlInitUnicodeString(&name, L"TpmExtractEK");
    tbs_register = MmGetSystemRoutineAddress(&name);
    return tbs_register != NULL ? TRUE : FALSE;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static sar_ppl_class_t SarDetectPplAvailability(VOID)
{
    RTL_OSVERSIONINFOW version;
    NTSTATUS status;

    RtlZeroMemory(&version, sizeof(version));
    version.dwOSVersionInfoSize = sizeof(version);
    status = RtlGetVersion(&version);
    if (!NT_SUCCESS(status))
        return SAR_PPL_NONE;

    if (version.dwMajorVersion > 6 ||
        (version.dwMajorVersion == 6 && version.dwMinorVersion >= 3))
        return SAR_PPL_AVAILABLE;
    return SAR_PPL_NONE;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarFeatureDetect(_Inout_ PSAR_POSTURE Posture)
{
    Posture->hvci_active = SarDetectHvci();
    Posture->vbs_active = SarDetectVbs();
    Posture->tpm20_present = SarDetectTpm20();
    Posture->ppl_available = SarDetectPplAvailability();
    Posture->dev_drive_supported = FALSE;
    Posture->dev_drive_attach_gap = FALSE;
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarFeatureDetectDevDrive(_In_ PCFLT_RELATED_OBJECTS FltObjects, _Inout_ PSAR_POSTURE Posture)
{
    PFLT_VOLUME volume = FltObjects->Volume;
    FLT_FILESYSTEM_TYPE fs_type = FLT_FSTYPE_UNKNOWN;
    NTSTATUS status;

    if (volume == NULL)
        return;

    status = FltGetFileSystemType(volume, &fs_type);
    if (!NT_SUCCESS(status))
        return;

    if (fs_type == FLT_FSTYPE_REFS) {
        Posture->dev_drive_supported = TRUE;
    }
}
