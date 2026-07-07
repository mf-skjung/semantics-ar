#include "driver.h"
#include "state.h"
#include "seam.h"
#include "feature.h"
#include "commport.h"
#include "capture.h"
#include "keystore_persist.h"
#include "preserve.h"
#include "phantom.h"
#include "eventlog.h"

SAR_GLOBALS g_sar;
PSAR_STATE g_sar_state;

DRIVER_INITIALIZE DriverEntry;

FLT_PREOP_CALLBACK_STATUS SarPreCreate(_Inout_ PFLT_CALLBACK_DATA Data,
                                       _In_ PCFLT_RELATED_OBJECTS FltObjects,
                                       _Flt_CompletionContext_Outptr_ PVOID *CompletionContext);
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
FLT_POSTOP_CALLBACK_STATUS SarPostRead(_Inout_ PFLT_CALLBACK_DATA Data,
                                       _In_ PCFLT_RELATED_OBJECTS FltObjects,
                                       _In_opt_ PVOID CompletionContext,
                                       _In_ FLT_POST_OPERATION_FLAGS Flags);
FLT_POSTOP_CALLBACK_STATUS SarPostCreate(_Inout_ PFLT_CALLBACK_DATA Data,
                                         _In_ PCFLT_RELATED_OBJECTS FltObjects,
                                         _In_opt_ PVOID CompletionContext,
                                         _In_ FLT_POST_OPERATION_FLAGS Flags);

VOID SarOsAnchorInit(VOID);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarBypassIoResolve(_Inout_ PSAR_POSTURE Posture);

_IRQL_requires_max_(PASSIVE_LEVEL)
static VOID SarStreamContextCleanup(_In_ PFLT_CONTEXT Context, _In_ FLT_CONTEXT_TYPE ContextType)
{
    PSAR_STREAM_CONTEXT sc = (PSAR_STREAM_CONTEXT)Context;

    UNREFERENCED_PARAMETER(ContextType);

    if (sc->cap_ranges != NULL) {
        ExFreePoolWithTag(sc->cap_ranges, SAR_POOL_TAG_STREAMCTX);
        sc->cap_ranges = NULL;
    }
    if (sc->mmap_map != NULL) {
        ExFreePoolWithTag(sc->mmap_map, SAR_POOL_TAG_STREAMCTX);
        sc->mmap_map = NULL;
    }
    if (sc->mmap_path != NULL) {
        ExFreePoolWithTag(sc->mmap_path, SAR_POOL_TAG_STREAMCTX);
        sc->mmap_path = NULL;
    }
    if (sc->mmap_reserved != 0) {
        SarPreserveRelease(g_sar.preserve, (UINT64)sc->mmap_reserved);
        sc->mmap_reserved = 0;
    }
    FltDeletePushLock(&sc->cap_lock);
}

static const FLT_OPERATION_REGISTRATION g_sar_operations[] = {
    { IRP_MJ_CREATE, 0, SarPreCreate, SarPostCreate },
    { IRP_MJ_READ, FLTFL_OPERATION_REGISTRATION_SKIP_PAGING_IO, NULL, SarPostRead },
    { IRP_MJ_WRITE, 0, SarPreWrite, NULL },
    { IRP_MJ_SET_INFORMATION, 0, SarPreSetInformation, NULL },
    { IRP_MJ_DIRECTORY_CONTROL, 0, SarPhantomPreDirControl, SarPhantomPostDirControl },
    { IRP_MJ_FILE_SYSTEM_CONTROL, 0, SarPreFsControl, SarPhantomPostFsControl },
    { IRP_MJ_QUERY_INFORMATION, 0, NULL, SarPhantomPostQueryInfo },
    { IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION, 0, SarPreAcquireForSection, NULL },
    { IRP_MJ_OPERATION_END }
};

static const FLT_CONTEXT_REGISTRATION g_sar_contexts[] = {
    { FLT_STREAM_CONTEXT, 0, SarStreamContextCleanup, sizeof(SAR_STREAM_CONTEXT), SAR_POOL_TAG_STREAMCTX },
    { FLT_STREAMHANDLE_CONTEXT, 0, NULL, sizeof(SAR_PHANTOM_ENUM_CONTEXT), SAR_POOL_TAG_PHANTOM },
    { FLT_SECTION_CONTEXT, 0, NULL, sizeof(LONG), SAR_POOL_TAG_SECTION },
    { FLT_INSTANCE_CONTEXT, 0, SarMmapInstanceCleanup, sizeof(SAR_MMAP_INSTCTX), SAR_POOL_TAG_STREAMCTX },
    { FLT_CONTEXT_END }
};

#ifndef FILE_DAX_VOLUME
#define FILE_DAX_VOLUME 0x20000000
#endif

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS SarInstanceSetup(_In_ PCFLT_RELATED_OBJECTS FltObjects,
                                 _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
                                 _In_ DEVICE_TYPE VolumeDeviceType,
                                 _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType)
{
    UCHAR attrbuf[sizeof(FILE_FS_ATTRIBUTE_INFORMATION) + 64];
    PFILE_FS_ATTRIBUTE_INFORMATION attr = (PFILE_FS_ATTRIBUTE_INFORMATION)attrbuf;
    IO_STATUS_BLOCK iosb;

    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(VolumeDeviceType);
    UNREFERENCED_PARAMETER(VolumeFilesystemType);

    if (NT_SUCCESS(FltQueryVolumeInformation(FltObjects->Instance, &iosb, attr, sizeof(attrbuf),
                                             FileFsAttributeInformation)) &&
        (attr->FileSystemAttributes & FILE_DAX_VOLUME) != 0)
        return STATUS_FLT_DO_NOT_ATTACH;

    SarFeatureDetectDevDrive(FltObjects, &g_sar.posture);
    FltRegisterForDataScan(FltObjects->Instance);
    SarMmapInstanceResolve(FltObjects->Filter, FltObjects);
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

    SarPhantomImageNotifyUnregister();
    SarProcessNotifyUnregister();

    if (g_sar.phantom != NULL) {
        SarPhantomDestroy(g_sar.phantom);
        g_sar.phantom = NULL;
    }

    if (g_sar.capture != NULL) {
        SarCaptureDestroy(g_sar.capture);
        g_sar.capture = NULL;
    }

    if (g_sar.preserve != NULL) {
        SarPreserveDestroy(g_sar.preserve);
        g_sar.preserve = NULL;
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

    if (g_sar.eventlog != NULL) {
        SarEventLogDestroy(g_sar.eventlog);
        g_sar.eventlog = NULL;
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
    if (g_sar.preserve != NULL)
        SarPreserveLoad(g_sar.preserve);
    if (g_sar.phantom != NULL && g_sar.keystore != NULL) {
        const UCHAR *mac_key = SarKeystoreMacKey(g_sar.keystore);
        if (mac_key != NULL) {
            SarPhantomActivate(g_sar.phantom, mac_key);
            g_sar.posture.phantom_active = TRUE;
        }
    }
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static BOOLEAN SarDriverIsPostBootStart(_In_ PUNICODE_STRING RegistryPath)
{
    OBJECT_ATTRIBUTES oa;
    HANDLE key = NULL;
    UNICODE_STRING name;
    UCHAR buf[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION info = (PKEY_VALUE_PARTIAL_INFORMATION)buf;
    ULONG ret = 0;
    ULONG start = 0;
    BOOLEAN post_boot = FALSE;

    InitializeObjectAttributes(&oa, RegistryPath, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               NULL, NULL);
    if (!NT_SUCCESS(ZwOpenKey(&key, KEY_READ, &oa)))
        return FALSE;

    RtlInitUnicodeString(&name, L"Start");
    if (NT_SUCCESS(ZwQueryValueKey(key, &name, KeyValuePartialInformation, info, sizeof(buf),
                                   &ret)) &&
        info->Type == REG_DWORD && info->DataLength == sizeof(ULONG)) {
        RtlCopyMemory(&start, info->Data, sizeof(ULONG));
        post_boot = (start >= SERVICE_AUTO_START);
    }

    ZwClose(key);
    return post_boot;
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

    status = SarEventLogCreate(&g_sar.eventlog);
    if (!NT_SUCCESS(status)) {
        SarStateDestroy(g_sar.state);
        g_sar.state = NULL;
        g_sar_state = NULL;
        return status;
    }

    status = FltRegisterFilter(DriverObject, &g_sar_registration, &g_sar.filter);
    if (!NT_SUCCESS(status)) {
        SarEventLogDestroy(g_sar.eventlog);
        g_sar.eventlog = NULL;
        SarStateDestroy(g_sar.state);
        g_sar.state = NULL;
        g_sar_state = NULL;
        return status;
    }

    status = SarProcessNotifyRegister(&g_sar.posture);
    if (!NT_SUCCESS(status)) {
        FltUnregisterFilter(g_sar.filter);
        g_sar.filter = NULL;
        SarEventLogDestroy(g_sar.eventlog);
        g_sar.eventlog = NULL;
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
        SarEventLogDestroy(g_sar.eventlog);
        g_sar.eventlog = NULL;
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
        SarEventLogDestroy(g_sar.eventlog);
        g_sar.eventlog = NULL;
        SarStateDestroy(g_sar.state);
        g_sar.state = NULL;
        g_sar_state = NULL;
        return status;
    }

    if (NT_SUCCESS(SarPhantomCreate(g_sar.filter, &g_sar.phantom)))
        SarPhantomImageNotifyRegister();
    else
        g_sar.phantom = NULL;

    status = SarCaptureCreate(g_sar.filter, &g_sar.capture);
    if (!NT_SUCCESS(status)) {
        if (g_sar.phantom != NULL) {
            SarPhantomImageNotifyUnregister();
            SarPhantomDestroy(g_sar.phantom);
            g_sar.phantom = NULL;
        }
        SarKeystoreDestroy(g_sar.keystore);
        g_sar.keystore = NULL;
        SarCommPortClose(g_sar.comm);
        g_sar.comm = NULL;
        SarProcessNotifyUnregister();
        FltUnregisterFilter(g_sar.filter);
        g_sar.filter = NULL;
        SarEventLogDestroy(g_sar.eventlog);
        g_sar.eventlog = NULL;
        SarStateDestroy(g_sar.state);
        g_sar.state = NULL;
        g_sar_state = NULL;
        return status;
    }

    if (NT_SUCCESS(SarPreserveCreate(g_sar.filter, &g_sar.posture, &g_sar.preserve)))
        g_sar.posture.preserve_active = TRUE;
    else
        g_sar.preserve = NULL;

    status = FltStartFiltering(g_sar.filter);
    if (!NT_SUCCESS(status)) {
        if (g_sar.preserve != NULL) {
            SarPreserveDestroy(g_sar.preserve);
            g_sar.preserve = NULL;
        }
        SarCaptureDestroy(g_sar.capture);
        g_sar.capture = NULL;
        if (g_sar.phantom != NULL) {
            SarPhantomImageNotifyUnregister();
            SarPhantomDestroy(g_sar.phantom);
            g_sar.phantom = NULL;
        }
        SarKeystoreDestroy(g_sar.keystore);
        g_sar.keystore = NULL;
        SarCommPortClose(g_sar.comm);
        g_sar.comm = NULL;
        SarProcessNotifyUnregister();
        FltUnregisterFilter(g_sar.filter);
        g_sar.filter = NULL;
        SarEventLogDestroy(g_sar.eventlog);
        g_sar.eventlog = NULL;
        SarStateDestroy(g_sar.state);
        g_sar.state = NULL;
        g_sar_state = NULL;
        return status;
    }

    SarOsAnchorInit();

    if (SarDriverIsPostBootStart(RegistryPath)) {
        SarKeystoreLoad(g_sar.keystore);
        if (g_sar.preserve != NULL)
            SarPreserveLoad(g_sar.preserve);
        if (g_sar.phantom != NULL && g_sar.keystore != NULL) {
            const UCHAR *mac_key = SarKeystoreMacKey(g_sar.keystore);
            if (mac_key != NULL) {
                SarPhantomActivate(g_sar.phantom, mac_key);
                g_sar.posture.phantom_active = TRUE;
            }
        }
    } else {
        IoRegisterBootDriverReinitialization(DriverObject, SarBootReinit, NULL);
    }

    return STATUS_SUCCESS;
}
