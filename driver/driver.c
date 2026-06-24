#include "driver.h"
#include "state.h"
#include "seam.h"
#include "feature.h"
#include "commport.h"
#include "capture.h"
#include "keystore_persist.h"

SAR_GLOBALS g_sar;
PSAR_STATE g_sar_state;

DRIVER_INITIALIZE DriverEntry;

FLT_PREOP_CALLBACK_STATUS SarPreWrite(_Inout_ PFLT_CALLBACK_DATA Data,
                                      _In_ PCFLT_RELATED_OBJECTS FltObjects,
                                      _Flt_CompletionContext_Outptr_ PVOID *CompletionContext);
FLT_PREOP_CALLBACK_STATUS SarPreSetInformation(_Inout_ PFLT_CALLBACK_DATA Data,
                                               _In_ PCFLT_RELATED_OBJECTS FltObjects,
                                               _Flt_CompletionContext_Outptr_ PVOID *CompletionContext);
FLT_PREOP_CALLBACK_STATUS SarPreFsControl(_Inout_ PFLT_CALLBACK_DATA Data,
                                          _In_ PCFLT_RELATED_OBJECTS FltObjects,
                                          _Flt_CompletionContext_Outptr_ PVOID *CompletionContext);
FLT_PREOP_CALLBACK_STATUS SarPreAcquireForSection(_Inout_ PFLT_CALLBACK_DATA Data,
                                                  _In_ PCFLT_RELATED_OBJECTS FltObjects,
                                                  _Flt_CompletionContext_Outptr_ PVOID *CompletionContext);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarBypassIoResolve(_Inout_ PSAR_POSTURE Posture);

_IRQL_requires_max_(PASSIVE_LEVEL)
static VOID SarStreamContextCleanup(_In_ PFLT_CONTEXT Context, _In_ FLT_CONTEXT_TYPE ContextType)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(ContextType);
}

static const FLT_OPERATION_REGISTRATION g_sar_operations[] = {
    { IRP_MJ_WRITE, 0, SarPreWrite, NULL },
    { IRP_MJ_SET_INFORMATION, 0, SarPreSetInformation, NULL },
    { IRP_MJ_FILE_SYSTEM_CONTROL, 0, SarPreFsControl, NULL },
    { IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION, 0, SarPreAcquireForSection, NULL },
    { IRP_MJ_OPERATION_END }
};

static const FLT_CONTEXT_REGISTRATION g_sar_contexts[] = {
    { FLT_STREAM_CONTEXT, 0, SarStreamContextCleanup, sizeof(SAR_STREAM_CONTEXT), SAR_POOL_TAG_STREAMCTX },
    { FLT_CONTEXT_END }
};

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS SarInstanceSetup(_In_ PCFLT_RELATED_OBJECTS FltObjects,
                                 _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
                                 _In_ DEVICE_TYPE VolumeDeviceType,
                                 _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType)
{
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(VolumeDeviceType);
    UNREFERENCED_PARAMETER(VolumeFilesystemType);

    SarFeatureDetectDevDrive(FltObjects, &g_sar.posture);
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS SarInstanceQueryTeardown(_In_ PCFLT_RELATED_OBJECTS FltObjects,
                                         _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Flags);
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS SarFilterUnload(_In_ FLT_FILTER_UNLOAD_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(Flags);

    SarProcessNotifyUnregister();

    if (g_sar.capture != NULL) {
        SarCaptureDestroy(g_sar.capture);
        g_sar.capture = NULL;
    }

    if (g_sar.keystore != NULL) {
        SarKeystoreDestroy(g_sar.keystore);
        g_sar.keystore = NULL;
    }

    if (g_sar.comm != NULL) {
        SarCommPortClose(g_sar.comm);
        g_sar.comm = NULL;
    }

    if (g_sar.filter != NULL) {
        FltUnregisterFilter(g_sar.filter);
        g_sar.filter = NULL;
    }

    if (g_sar.state != NULL) {
        SarStateDestroy(g_sar.state);
        g_sar.state = NULL;
        g_sar_state = NULL;
    }

    return STATUS_SUCCESS;
}

_Function_class_(DRIVER_REINITIALIZE)
_IRQL_requires_max_(PASSIVE_LEVEL)
static VOID SarBootReinit(_In_ PDRIVER_OBJECT DriverObject, _In_opt_ PVOID Context, _In_ ULONG Count)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Count);

    if (g_sar.keystore != NULL)
        SarKeystoreLoad(g_sar.keystore);
}

static const FLT_REGISTRATION g_sar_registration = {
    sizeof(FLT_REGISTRATION),
    FLT_REGISTRATION_VERSION,
    0,
    g_sar_contexts,
    g_sar_operations,
    SarFilterUnload,
    SarInstanceSetup,
    SarInstanceQueryTeardown,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};

_Use_decl_annotations_
NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS status;

    UNREFERENCED_PARAMETER(RegistryPath);

    RtlZeroMemory(&g_sar, sizeof(g_sar));
    g_sar.driver_object = DriverObject;

    SarSeamCoverageReset();

    status = SarFeatureDetect(&g_sar.posture);
    if (!NT_SUCCESS(status))
        return status;

    SarBypassIoResolve(&g_sar.posture);

    status = SarStateCreate(&g_sar.state);
    if (!NT_SUCCESS(status))
        return status;
    g_sar_state = g_sar.state;

    status = FltRegisterFilter(DriverObject, &g_sar_registration, &g_sar.filter);
    if (!NT_SUCCESS(status)) {
        SarStateDestroy(g_sar.state);
        g_sar.state = NULL;
        g_sar_state = NULL;
        return status;
    }

    status = SarProcessNotifyRegister(&g_sar.posture);
    if (!NT_SUCCESS(status)) {
        FltUnregisterFilter(g_sar.filter);
        g_sar.filter = NULL;
        SarStateDestroy(g_sar.state);
        g_sar.state = NULL;
        g_sar_state = NULL;
        return status;
    }
    g_sar.process_notify_registered = TRUE;

    status = SarCommPortCreate(g_sar.filter, &g_sar.comm);
    if (!NT_SUCCESS(status)) {
        SarProcessNotifyUnregister();
        FltUnregisterFilter(g_sar.filter);
        g_sar.filter = NULL;
        SarStateDestroy(g_sar.state);
        g_sar.state = NULL;
        g_sar_state = NULL;
        return status;
    }

    status = SarKeystoreCreate(g_sar.filter, &g_sar.posture, &g_sar.keystore);
    if (!NT_SUCCESS(status)) {
        SarCommPortClose(g_sar.comm);
        g_sar.comm = NULL;
        SarProcessNotifyUnregister();
        FltUnregisterFilter(g_sar.filter);
        g_sar.filter = NULL;
        SarStateDestroy(g_sar.state);
        g_sar.state = NULL;
        g_sar_state = NULL;
        return status;
    }

    status = SarCaptureCreate(g_sar.filter, &g_sar.capture);
    if (!NT_SUCCESS(status)) {
        SarKeystoreDestroy(g_sar.keystore);
        g_sar.keystore = NULL;
        SarCommPortClose(g_sar.comm);
        g_sar.comm = NULL;
        SarProcessNotifyUnregister();
        FltUnregisterFilter(g_sar.filter);
        g_sar.filter = NULL;
        SarStateDestroy(g_sar.state);
        g_sar.state = NULL;
        g_sar_state = NULL;
        return status;
    }

    status = FltStartFiltering(g_sar.filter);
    if (!NT_SUCCESS(status)) {
        SarCaptureDestroy(g_sar.capture);
        g_sar.capture = NULL;
        SarKeystoreDestroy(g_sar.keystore);
        g_sar.keystore = NULL;
        SarCommPortClose(g_sar.comm);
        g_sar.comm = NULL;
        SarProcessNotifyUnregister();
        FltUnregisterFilter(g_sar.filter);
        g_sar.filter = NULL;
        SarStateDestroy(g_sar.state);
        g_sar.state = NULL;
        g_sar_state = NULL;
        return status;
    }

    IoRegisterBootDriverReinitialization(DriverObject, SarBootReinit, NULL);

    return STATUS_SUCCESS;
}
