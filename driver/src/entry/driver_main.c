#include "driver_internal.h"

semantics_ar_global_data_t semantics_ar_globals;

static VOID semantics_ar_instance_context_cleanup(PFLT_CONTEXT Context, FLT_CONTEXT_TYPE ContextType)
{
    semantics_ar_instance_context_t *instCtx = (semantics_ar_instance_context_t *)Context;
    UNREFERENCED_PARAMETER(ContextType);
    if (instCtx->JournalResourceInitialized)
        ExDeleteResourceLite(&instCtx->JournalResource);
}

static const FLT_CONTEXT_REGISTRATION context_registration[] = {
    { FLT_STREAM_CONTEXT, 0, semantics_ar_stream_context_cleanup,
      sizeof(semantics_ar_stream_context_t), SEMANTICS_AR_POOL_TAG },
    { FLT_INSTANCE_CONTEXT, 0, semantics_ar_instance_context_cleanup,
      sizeof(semantics_ar_instance_context_t), SEMANTICS_AR_POOL_TAG },
    { FLT_CONTEXT_END }
};

static const FLT_OPERATION_REGISTRATION operation_callbacks[] = {
    { IRP_MJ_CREATE, 0, semantics_ar_pre_create, semantics_ar_post_create },
    { IRP_MJ_WRITE, 0, semantics_ar_pre_write, NULL },
    { IRP_MJ_SET_INFORMATION, 0, semantics_ar_pre_set_information, NULL },
    { IRP_MJ_FILE_SYSTEM_CONTROL, 0, semantics_ar_pre_fsctl, NULL },
    { IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION, 0, semantics_ar_pre_acquire_section, NULL },
    { IRP_MJ_OPERATION_END }
};

static const FLT_REGISTRATION filter_registration = {
    sizeof(FLT_REGISTRATION), FLT_REGISTRATION_VERSION, 0,
    context_registration, operation_callbacks,
    semantics_ar_filter_unload,
    semantics_ar_instance_setup, NULL,
    semantics_ar_instance_teardown_start, semantics_ar_instance_teardown_complete,
    NULL, NULL, NULL
};

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    NTSTATUS status;
    UNREFERENCED_PARAMETER(RegistryPath);

    ExInitializeDriverRuntime(DrvRtPoolNxOptIn);
    RtlZeroMemory(&semantics_ar_globals, sizeof(semantics_ar_globals));

    ExInitializeFastMutex(&semantics_ar_globals.ConfigLock);
    ExInitializeFastMutex(&semantics_ar_globals.ProcessMonitorLock);
    semantics_ar_globals.Config.timeout_ms = SEMANTICS_AR_DEFAULT_REPLY_TIMEOUT_MS;
    semantics_ar_globals.ShadowSequence = 0;
    semantics_ar_globals.CaptureWorkerCount = 0;

    status = semantics_ar_mac_init();
    if (!NT_SUCCESS(status))
        return status;

    semantics_ar_globals.ProcessMonitors = (semantics_ar_process_monitor_t *)ExAllocatePoolZero(
        PagedPool, sizeof(semantics_ar_process_monitor_t) * SEMANTICS_AR_MAX_PROCESS_MONITORS,
        SEMANTICS_AR_POOL_TAG_PG);
    if (!semantics_ar_globals.ProcessMonitors) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto cleanup_mac;
    }

    semantics_ar_globals.CreateRingEntries = (semantics_ar_create_ring_entry_t *)ExAllocatePoolZero(
        NonPagedPoolNx, sizeof(semantics_ar_create_ring_entry_t) * SEMANTICS_AR_CREATE_RING_SIZE,
        SEMANTICS_AR_POOL_TAG);
    if (!semantics_ar_globals.CreateRingEntries) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto cleanup_resources;
    }

    status = FltRegisterFilter(DriverObject, &filter_registration,
        &semantics_ar_globals.FilterHandle);
    if (!NT_SUCCESS(status))
        goto cleanup_resources;

    status = semantics_ar_create_comm_port(semantics_ar_globals.FilterHandle);
    if (!NT_SUCCESS(status))
        goto cleanup_filter;

    status = PsSetCreateProcessNotifyRoutineEx(semantics_ar_process_notify, FALSE);
    if (!NT_SUCCESS(status))
        goto cleanup_port;
    semantics_ar_globals.ProcessNotifyRegistered = TRUE;

    status = FltStartFiltering(semantics_ar_globals.FilterHandle);
    if (!NT_SUCCESS(status))
        goto cleanup_notify;

    return STATUS_SUCCESS;

cleanup_notify:
    PsSetCreateProcessNotifyRoutineEx(semantics_ar_process_notify, TRUE);
    semantics_ar_globals.ProcessNotifyRegistered = FALSE;
cleanup_port:
    semantics_ar_close_comm_port();
cleanup_filter:
    FltUnregisterFilter(semantics_ar_globals.FilterHandle);
    semantics_ar_globals.FilterHandle = NULL;
cleanup_resources:
    if (semantics_ar_globals.CreateRingEntries) {
        ExFreePoolWithTag(semantics_ar_globals.CreateRingEntries, SEMANTICS_AR_POOL_TAG);
        semantics_ar_globals.CreateRingEntries = NULL;
    }
    if (semantics_ar_globals.ProcessMonitors) {
        ExFreePoolWithTag(semantics_ar_globals.ProcessMonitors, SEMANTICS_AR_POOL_TAG_PG);
        semantics_ar_globals.ProcessMonitors = NULL;
    }
cleanup_mac:
    semantics_ar_mac_cleanup();
    return status;
}

NTSTATUS semantics_ar_filter_unload(FLT_FILTER_UNLOAD_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(Flags);

    if (semantics_ar_globals.ProcessNotifyRegistered) {
        PsSetCreateProcessNotifyRoutineEx(semantics_ar_process_notify, TRUE);
        semantics_ar_globals.ProcessNotifyRegistered = FALSE;
    }

    semantics_ar_close_comm_port();
    FltUnregisterFilter(semantics_ar_globals.FilterHandle);
    semantics_ar_globals.FilterHandle = NULL;

    {
        LARGE_INTEGER d;
        d.QuadPart = SEMANTICS_AR_JOURNAL_DRAIN_RECHECK_100NS;
        for (LONG i = 0; i < 500; i++) {
            if (semantics_ar_globals.CaptureWorkerCount == 0)
                break;
            KeDelayExecutionThread(KernelMode, FALSE, &d);
        }
    }

    if (semantics_ar_globals.ProcessMonitors) {
        for (LONG i = 0; i < SEMANTICS_AR_MAX_PROCESS_MONITORS; i++) {
            if (semantics_ar_globals.ProcessMonitors[i].CreatedFileRing)
                ExFreePoolWithTag(semantics_ar_globals.ProcessMonitors[i].CreatedFileRing,
                    SEMANTICS_AR_POOL_TAG_PG);
        }
        ExFreePoolWithTag(semantics_ar_globals.ProcessMonitors, SEMANTICS_AR_POOL_TAG_PG);
        semantics_ar_globals.ProcessMonitors = NULL;
    }

    if (semantics_ar_globals.CreateRingEntries) {
        ExFreePoolWithTag(semantics_ar_globals.CreateRingEntries, SEMANTICS_AR_POOL_TAG);
        semantics_ar_globals.CreateRingEntries = NULL;
    }

    semantics_ar_mac_cleanup();
    return STATUS_SUCCESS;
}

NTSTATUS semantics_ar_instance_setup(
    PCFLT_RELATED_OBJECTS FltObjects,
    FLT_INSTANCE_SETUP_FLAGS Flags,
    DEVICE_TYPE VolumeDeviceType,
    FLT_FILESYSTEM_TYPE VolumeFilesystemType)
{
    semantics_ar_instance_context_t *instCtx = NULL;
    NTSTATUS status;
    WCHAR volNameBuf[256];
    UNICODE_STRING volName;
    UNREFERENCED_PARAMETER(Flags);

    if (VolumeDeviceType == FILE_DEVICE_NETWORK_FILE_SYSTEM)
        return STATUS_FLT_DO_NOT_ATTACH;
    if (VolumeFilesystemType != FLT_FSTYPE_NTFS &&
        VolumeFilesystemType != FLT_FSTYPE_REFS)
        return STATUS_FLT_DO_NOT_ATTACH;

    status = FltAllocateContext(semantics_ar_globals.FilterHandle,
        FLT_INSTANCE_CONTEXT, sizeof(semantics_ar_instance_context_t),
        NonPagedPool, (PFLT_CONTEXT *)&instCtx);
    if (!NT_SUCCESS(status))
        return STATUS_FLT_DO_NOT_ATTACH;

    RtlZeroMemory(instCtx, sizeof(*instCtx));

    status = ExInitializeResourceLite(&instCtx->JournalResource);
    if (!NT_SUCCESS(status)) {
        FltReleaseContext(instCtx);
        return STATUS_FLT_DO_NOT_ATTACH;
    }
    instCtx->JournalResourceInitialized = TRUE;
    KeInitializeEvent(&instCtx->JournalDrainEvent, NotificationEvent, TRUE);
    instCtx->SectorSize = SEMANTICS_AR_DEFAULT_SECTOR;
    instCtx->ClusterSize = SEMANTICS_AR_DEFAULT_SECTOR;
    instCtx->ShadowMaxBytes = SEMANTICS_AR_SHADOW_MIN_BYTES;

    volName.Buffer = volNameBuf;
    volName.Length = 0;
    volName.MaximumLength = sizeof(volNameBuf);
    status = FltGetVolumeName(FltObjects->Volume, &volName, NULL);
    if (!NT_SUCCESS(status)) {
        FltReleaseContext(instCtx);
        return STATUS_FLT_DO_NOT_ATTACH;
    }

    instCtx->ShadowBasePath.Buffer = instCtx->ShadowBasePathBuffer;
    instCtx->ShadowBasePath.Length = 0;
    instCtx->ShadowBasePath.MaximumLength = sizeof(instCtx->ShadowBasePathBuffer);
    RtlCopyUnicodeString(&instCtx->ShadowBasePath, &volName);
    RtlAppendUnicodeToString(&instCtx->ShadowBasePath, L"\\" SEMANTICS_AR_SHADOW_DIR_NAME);

    FltSetInstanceContext(FltObjects->Instance, FLT_SET_CONTEXT_KEEP_IF_EXISTS, instCtx, NULL);
    FltReleaseContext(instCtx);
    return STATUS_SUCCESS;
}

VOID semantics_ar_instance_teardown_start(
    PCFLT_RELATED_OBJECTS FltObjects,
    FLT_INSTANCE_TEARDOWN_FLAGS Reason)
{
    semantics_ar_instance_context_t *instCtx = NULL;
    UNREFERENCED_PARAMETER(Reason);
    if (NT_SUCCESS(FltGetInstanceContext(FltObjects->Instance, (PFLT_CONTEXT *)&instCtx))) {
        InterlockedExchange(&instCtx->AcceptNewJournalWrites, 0);
        FltReleaseContext(instCtx);
    }
}

VOID semantics_ar_instance_teardown_complete(
    PCFLT_RELATED_OBJECTS FltObjects,
    FLT_INSTANCE_TEARDOWN_FLAGS Reason)
{
    semantics_ar_instance_context_t *instCtx = NULL;
    UNREFERENCED_PARAMETER(Reason);
    if (!NT_SUCCESS(FltGetInstanceContext(FltObjects->Instance, (PFLT_CONTEXT *)&instCtx)))
        return;
    if (instCtx->JournalHandle) {
        FltClose(instCtx->JournalHandle);
        instCtx->JournalHandle = NULL;
    }
    if (instCtx->JournalFileObject) {
        ObDereferenceObject(instCtx->JournalFileObject);
        instCtx->JournalFileObject = NULL;
    }
    FltReleaseContext(instCtx);
}