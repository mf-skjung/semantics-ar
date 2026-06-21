#include "driver_internal.h"

#define SAR_PROCESS_PROTECTION_INFORMATION ((PROCESSINFOCLASS)61)

typedef struct _PS_PROTECTION_LOCAL {
    union {
        UCHAR Level;
        struct {
            UCHAR Type   : 3;
            UCHAR Audit  : 1;
            UCHAR Signer : 4;
        };
    };
} PS_PROTECTION_LOCAL;

#define PS_SIGNER_ANTIMALWARE 3
#define PS_SIGNER_WINDOWS     5
#define PS_SIGNER_WINTCB      6

static BOOLEAN verify_connecting_client(VOID)
{
    HANDLE pid = PsGetCurrentProcessId();
    PEPROCESS process = NULL;
    if (!NT_SUCCESS(PsLookupProcessByProcessId(pid, &process)))
        return FALSE;

    HANDLE hProc = NULL;
    NTSTATUS status = ObOpenObjectByPointer(process, OBJ_KERNEL_HANDLE, NULL,
        PROCESS_QUERY_INFORMATION, *PsProcessType, KernelMode, &hProc);
    ObDereferenceObject(process);
    if (!NT_SUCCESS(status))
        return FALSE;

    PS_PROTECTION_LOCAL prot;
    RtlZeroMemory(&prot, sizeof(prot));
    ULONG rl = 0;
    status = ZwQueryInformationProcess(hProc, SAR_PROCESS_PROTECTION_INFORMATION,
        &prot, sizeof(prot), &rl);
    ZwClose(hProc);

    if (!NT_SUCCESS(status))
        return FALSE;

    if (prot.Signer == PS_SIGNER_ANTIMALWARE ||
        prot.Signer == PS_SIGNER_WINDOWS ||
        prot.Signer == PS_SIGNER_WINTCB)
        return TRUE;

    return FALSE;
}

static NTSTATUS connect_notify(
    PFLT_PORT ClientPort, PVOID ServerPortCookie, PVOID ConnectionContext,
    ULONG SizeOfContext, PVOID *ConnectionCookie)
{
    UNREFERENCED_PARAMETER(ServerPortCookie);
    UNREFERENCED_PARAMETER(ConnectionContext);
    UNREFERENCED_PARAMETER(SizeOfContext);

    if (!verify_connecting_client())
        return STATUS_ACCESS_DENIED;

    HANDLE pid = PsGetCurrentProcessId();
    PEPROCESS process = NULL;
    LONGLONG creationTime = 0;
    if (NT_SUCCESS(PsLookupProcessByProcessId(pid, &process))) {
        creationTime = PsGetProcessCreateTimeQuadPart(process);
        ObDereferenceObject(process);
    }

    semantics_ar_globals.ClientPort = ClientPort;
    semantics_ar_globals.ServiceProcessId = HandleToULong(pid);
    semantics_ar_globals.ServiceCreationTime = creationTime;
    InterlockedExchange(&semantics_ar_globals.ProtectionActive, 1);
    *ConnectionCookie = NULL;
    return STATUS_SUCCESS;
}

static VOID disconnect_notify(PVOID ConnectionCookie)
{
    UNREFERENCED_PARAMETER(ConnectionCookie);
    InterlockedExchange(&semantics_ar_globals.ProtectionActive, 0);
    semantics_ar_globals.ServiceProcessId = 0;
    semantics_ar_globals.ServiceCreationTime = 0;
    RtlZeroMemory(semantics_ar_globals.TrustedPids, sizeof(semantics_ar_globals.TrustedPids));
    FltCloseClientPort(semantics_ar_globals.FilterHandle, &semantics_ar_globals.ClientPort);
}

static NTSTATUS handle_confirmed(const void *payload, ULONG size)
{
    if (size < sizeof(semantics_ar_confirm_payload_t))
        return STATUS_INVALID_PARAMETER;
    const semantics_ar_confirm_payload_t *p = (const semantics_ar_confirm_payload_t *)payload;
    semantics_ar_confirm_process_tree(p->target_pid);
    semantics_ar_drain_journal_all_instances();
    return STATUS_SUCCESS;
}

static NTSTATUS handle_restore_complete(const void *payload, ULONG size)
{
    if (size < sizeof(semantics_ar_confirm_payload_t))
        return STATUS_INVALID_PARAMETER;
    const semantics_ar_confirm_payload_t *p = (const semantics_ar_confirm_payload_t *)payload;

    ExAcquireFastMutex(&semantics_ar_globals.ProcessMonitorLock);
    semantics_ar_release_confirmed_tree_locked(p->target_pid);
    BOOLEAN anyConfirmed = FALSE;
    for (LONG i = 0; i < SEMANTICS_AR_MAX_PROCESS_MONITORS; i++) {
        if (semantics_ar_globals.ProcessMonitors[i].Pid != 0 &&
            semantics_ar_globals.ProcessMonitors[i].State == SEMANTICS_AR_PROC_CONFIRMED) {
            anyConfirmed = TRUE;
            break;
        }
    }
    if (!anyConfirmed)
        InterlockedExchange(&semantics_ar_globals.ConfirmedActive, 0);
    ExReleaseFastMutex(&semantics_ar_globals.ProcessMonitorLock);
    return STATUS_SUCCESS;
}

static NTSTATUS handle_associate_child(const void *payload, ULONG size)
{
    if (size < sizeof(semantics_ar_associate_child_payload_t))
        return STATUS_INVALID_PARAMETER;
    const semantics_ar_associate_child_payload_t *p =
        (const semantics_ar_associate_child_payload_t *)payload;
    ExAcquireFastMutex(&semantics_ar_globals.ProcessMonitorLock);
    semantics_ar_associate_child_locked(p->child_pid, p->originator_pid);
    ExReleaseFastMutex(&semantics_ar_globals.ProcessMonitorLock);
    return STATUS_SUCCESS;
}

static NTSTATUS handle_set_config(const void *payload, ULONG size)
{
    if (size < sizeof(semantics_ar_config_t))
        return STATUS_INVALID_PARAMETER;
    ExAcquireFastMutex(&semantics_ar_globals.ConfigLock);
    RtlCopyMemory(&semantics_ar_globals.Config, payload, sizeof(semantics_ar_config_t));
    ExReleaseFastMutex(&semantics_ar_globals.ConfigLock);
    return STATUS_SUCCESS;
}

static NTSTATUS handle_add_trusted_pid(const void *payload, ULONG size)
{
    if (size < sizeof(semantics_ar_trusted_pid_info_t))
        return STATUS_INVALID_PARAMETER;
    const semantics_ar_trusted_pid_info_t *info = (const semantics_ar_trusted_pid_info_t *)payload;
    semantics_ar_add_trusted_pid(info->pid, info->creation_time);
    return STATUS_SUCCESS;
}

static NTSTATUS handle_remove_trusted_pid(const void *payload, ULONG size)
{
    if (size < sizeof(semantics_ar_trusted_pid_info_t))
        return STATUS_INVALID_PARAMETER;
    const semantics_ar_trusted_pid_info_t *info = (const semantics_ar_trusted_pid_info_t *)payload;
    semantics_ar_remove_trusted_pid(info->pid, info->creation_time);
    return STATUS_SUCCESS;
}

static NTSTATUS handle_get_status(PVOID OutputBuffer, ULONG OutputBufferSize, PULONG ReturnLength)
{
    semantics_ar_status_reply_t reply;
    if (OutputBuffer == NULL || OutputBufferSize < sizeof(reply))
        return STATUS_BUFFER_TOO_SMALL;

    RtlZeroMemory(&reply, sizeof(reply));
    reply.result_code = SEMANTICS_AR_OK;
    reply.protection_active = semantics_ar_globals.ProtectionActive ? 1 : 0;
    reply.confirmed_active = semantics_ar_globals.ConfirmedActive ? 1 : 0;
    for (LONG i = 0; i < SEMANTICS_AR_MAX_TRUSTED_PIDS; i++)
        if (semantics_ar_globals.TrustedPids[i].Pid != 0)
            reply.trusted_pid_count++;

    RtlCopyMemory(OutputBuffer, &reply, sizeof(reply));
    *ReturnLength = sizeof(reply);
    return STATUS_SUCCESS;
}

static NTSTATUS message_notify(
    PVOID ConnectionCookie, PVOID InputBuffer, ULONG InputBufferSize,
    PVOID OutputBuffer, ULONG OutputBufferSize, PULONG ReturnOutputBufferLength)
{
    PVOID safeInput = NULL;
    UINT32 messageType;
    NTSTATUS status;
    ULONG payloadSize;
    const void *payload;
    UNREFERENCED_PARAMETER(ConnectionCookie);

    *ReturnOutputBufferLength = 0;

    if (InputBuffer == NULL || InputBufferSize < sizeof(semantics_ar_msg_header_t))
        return STATUS_INVALID_PARAMETER;

    safeInput = ExAllocatePoolZero(NonPagedPoolNx, InputBufferSize, SEMANTICS_AR_POOL_TAG);
    if (safeInput == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    __try {
        ProbeForRead(InputBuffer, InputBufferSize, sizeof(UCHAR));
        RtlCopyMemory(safeInput, InputBuffer, InputBufferSize);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ExFreePoolWithTag(safeInput, SEMANTICS_AR_POOL_TAG);
        return STATUS_INVALID_USER_BUFFER;
    }

    messageType = ((semantics_ar_msg_header_t *)safeInput)->message_type;
    payloadSize = InputBufferSize - sizeof(semantics_ar_msg_header_t);
    payload = (PUCHAR)safeInput + sizeof(semantics_ar_msg_header_t);

    switch (messageType) {
    case SEMANTICS_AR_MSG_CONFIRMED:
        status = handle_confirmed(payload, payloadSize);
        break;
    case SEMANTICS_AR_MSG_RESTORE_COMPLETE:
        status = handle_restore_complete(payload, payloadSize);
        break;
    case SEMANTICS_AR_MSG_ASSOCIATE_CHILD:
        status = handle_associate_child(payload, payloadSize);
        break;
    case SEMANTICS_AR_MSG_SET_CONFIG:
        status = handle_set_config(payload, payloadSize);
        break;
    case SEMANTICS_AR_MSG_ADD_TRUSTED_PID:
        status = handle_add_trusted_pid(payload, payloadSize);
        break;
    case SEMANTICS_AR_MSG_REMOVE_TRUSTED_PID:
        status = handle_remove_trusted_pid(payload, payloadSize);
        break;
    case SEMANTICS_AR_MSG_GET_STATUS:
        status = handle_get_status(OutputBuffer, OutputBufferSize, ReturnOutputBufferLength);
        break;
    default:
        status = STATUS_INVALID_PARAMETER;
        break;
    }

    ExFreePoolWithTag(safeInput, SEMANTICS_AR_POOL_TAG);
    return status;
}

NTSTATUS semantics_ar_create_comm_port(PFLT_FILTER Filter)
{
    NTSTATUS status;
    UNICODE_STRING portName;
    OBJECT_ATTRIBUTES oa;
    PSECURITY_DESCRIPTOR sd = NULL;

    status = FltBuildDefaultSecurityDescriptor(&sd, FLT_PORT_ALL_ACCESS);
    if (!NT_SUCCESS(status))
        return status;

    RtlInitUnicodeString(&portName, SEMANTICS_AR_PORT_NAME);
    InitializeObjectAttributes(&oa, &portName,
        OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, sd);
    status = FltCreateCommunicationPort(
        Filter, &semantics_ar_globals.ServerPort, &oa, NULL,
        connect_notify, disconnect_notify, message_notify, 1);
    FltFreeSecurityDescriptor(sd);
    return status;
}

VOID semantics_ar_close_comm_port(VOID)
{
    if (semantics_ar_globals.ServerPort != NULL) {
        FltCloseCommunicationPort(semantics_ar_globals.ServerPort);
        semantics_ar_globals.ServerPort = NULL;
    }
}