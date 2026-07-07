#include "commport.h"
#include "state.h"
#include "ntsystem.h"
#include "recovery.h"
#include "keystore_persist.h"
#include "preserve.h"
#include "eventlog.h"

#include <bcrypt.h>
#include <ntifs.h>

extern PSAR_STATE g_sar_state;

static const WCHAR g_sar_service_image_allow[] = L"\\semantics_ar_service.exe";

#include "service_pubkey.h"

_IRQL_requires_max_(PASSIVE_LEVEL)
static int SarHandshakeVerify(const uint8_t *nonce, uint32_t nonce_len,
                              const uint8_t *signature, uint32_t sig_len, void *ctx)
{
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_KEY_HANDLE key = NULL;
    NTSTATUS status;
    int result = 0;

    UNREFERENCED_PARAMETER(ctx);

    if (nonce == NULL || signature == NULL)
        return 0;

    status = BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, NULL, 0);
    if (!NT_SUCCESS(status))
        return 0;

    status = BCryptImportKeyPair(alg, NULL, BCRYPT_ECCPUBLIC_BLOB, &key,
                                 (PUCHAR)g_sar_service_public_key,
                                 (ULONG)sizeof(g_sar_service_public_key), 0);
    if (NT_SUCCESS(status)) {
        status = BCryptVerifySignature(key, NULL,
                                       (PUCHAR)nonce, nonce_len,
                                       (PUCHAR)signature, sig_len, 0);
        if (NT_SUCCESS(status))
            result = 1;
        BCryptDestroyKey(key);
    }

    BCryptCloseAlgorithmProvider(alg, 0);
    return result;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS SarResolveConnectorImage(_In_ HANDLE ProcessId,
                                         _Out_ PUNICODE_STRING *ImageName)
{
    PEPROCESS process = NULL;
    PUNICODE_STRING image = NULL;
    NTSTATUS status;

    *ImageName = NULL;

    status = PsLookupProcessByProcessId(ProcessId, &process);
    if (!NT_SUCCESS(status))
        return status;

    status = SeLocateProcessImageName(process, &image);
    ObDereferenceObject(process);
    if (!NT_SUCCESS(status))
        return status;

    *ImageName = image;
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static BOOLEAN SarImagePathAllowed(_In_ PUNICODE_STRING ImageName)
{
    UNICODE_STRING suffix;

    RtlInitUnicodeString(&suffix, g_sar_service_image_allow);
    if (ImageName->Length < suffix.Length)
        return FALSE;

    UNICODE_STRING tail;
    tail.Buffer = (PWCH)((PUCHAR)ImageName->Buffer + (ImageName->Length - suffix.Length));
    tail.Length = suffix.Length;
    tail.MaximumLength = suffix.Length;
    return RtlEqualUnicodeString(&tail, &suffix, TRUE);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static sar_ppl_class_t SarQueryConnectorProtection(_In_ HANDLE ProcessId)
{
    PEPROCESS process = NULL;
    HANDLE handle = NULL;
    NTSTATUS status;
    PS_PROTECTION protection;
    ULONG returned = 0;
    sar_ppl_class_t result = SAR_PPL_NONE;
    OBJECT_ATTRIBUTES attr;
    CLIENT_ID client;

    status = PsLookupProcessByProcessId(ProcessId, &process);
    if (!NT_SUCCESS(status))
        return SAR_PPL_NONE;

    client.UniqueProcess = ProcessId;
    client.UniqueThread = NULL;
    InitializeObjectAttributes(&attr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

    status = ZwOpenProcess(&handle, PROCESS_QUERY_LIMITED_INFORMATION, &attr, &client);
    if (NT_SUCCESS(status)) {
        RtlZeroMemory(&protection, sizeof(protection));
        status = ZwQueryInformationProcess(handle, ProcessProtectionInformation,
                                           &protection, sizeof(protection), &returned);
        if (NT_SUCCESS(status) && returned >= sizeof(protection)) {
            if (protection.Type == PsProtectedTypeProtected &&
                protection.Signer == PsProtectedSignerAntimalware)
                result = SAR_PPL_ANTIMALWARE;
            else if (protection.Type != PsProtectedTypeNone)
                result = SAR_PPL_AVAILABLE;
        }
        ZwClose(handle);
    }

    ObDereferenceObject(process);
    return result;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS SarIssueChallenge(_Inout_ PSAR_COMM Comm)
{
    UCHAR nonce[SAR_HS_NONCE_SIZE];
    semantics_ar_connect_challenge_t challenge;
    NTSTATUS status;
    sar_hs_result_t hs;

    status = BCryptGenRandom(NULL, nonce, SAR_HS_NONCE_SIZE,
                             BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (!NT_SUCCESS(status))
        return status;

    hs = sar_handshake_issue_challenge(&Comm->handshake, nonce, SAR_HS_NONCE_SIZE);
    if (hs != SAR_HS_RESULT_OK)
        return STATUS_UNSUCCESSFUL;

    RtlZeroMemory(&challenge, sizeof(challenge));
    challenge.header.protocol_version = SEMANTICS_AR_PROTOCOL_VERSION;
    challenge.header.message_type = SEMANTICS_AR_MSG_CONNECT_CHALLENGE;
    challenge.header.message_length = (uint32_t)sizeof(challenge);
    RtlCopyMemory(challenge.nonce, nonce, SAR_HS_NONCE_SIZE);

    return FltSendMessage(Comm->filter, &Comm->client_port, &challenge, sizeof(challenge),
                          NULL, NULL, NULL);
}

_Function_class_(FLT_GENERIC_WORKITEM_ROUTINE)
static VOID SarChallengeWorker(_In_ PFLT_GENERIC_WORKITEM FltWorkItem,
                               _In_ PVOID FltObject, _In_opt_ PVOID Context)
{
    PSAR_COMM comm = (PSAR_COMM)Context;
    UNREFERENCED_PARAMETER(FltObject);
    if (comm != NULL)
        (VOID)SarIssueChallenge(comm);
    FltFreeGenericWorkItem(FltWorkItem);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarCommConnectNotify(_In_ PFLT_PORT ClientPort,
                              _In_opt_ PVOID ServerCookie,
                              _In_reads_bytes_opt_(SizeOfContext) PVOID ConnectionContext,
                              _In_ ULONG SizeOfContext,
                              _Outptr_result_maybenull_ PVOID *ConnectionCookie)
{
    PSAR_COMM comm = (PSAR_COMM)ServerCookie;
    PUNICODE_STRING image = NULL;
    HANDLE connector_pid;
    NTSTATUS status;
    sar_ppl_class_t protection;

    UNREFERENCED_PARAMETER(ConnectionContext);
    UNREFERENCED_PARAMETER(SizeOfContext);

    *ConnectionCookie = NULL;

    if (comm == NULL)
        goto reject;

    connector_pid = PsGetCurrentProcessId();

    status = SarResolveConnectorImage(connector_pid, &image);
    if (!NT_SUCCESS(status))
        goto reject;

    if (!SarImagePathAllowed(image)) {
        ExFreePool(image);
        goto reject;
    }
    ExFreePool(image);

    sar_handshake_init(&comm->handshake, SEMANTICS_AR_PROTOCOL_VERSION,
                       SarHandshakeVerify, NULL);

    comm->client_port = ClientPort;

    {
        PFLT_GENERIC_WORKITEM challenge_wi = FltAllocateGenericWorkItem();
        if (challenge_wi == NULL) {
            comm->client_port = NULL;
            goto reject;
        }
        status = FltQueueGenericWorkItem(challenge_wi, comm->filter, SarChallengeWorker,
                                         DelayedWorkQueue, comm);
        if (!NT_SUCCESS(status)) {
            FltFreeGenericWorkItem(challenge_wi);
            comm->client_port = NULL;
            goto reject;
        }
    }

    protection = SarQueryConnectorProtection(connector_pid);
    if (protection == SAR_PPL_ANTIMALWARE)
        g_sar.posture.ppl_available = SAR_PPL_ANTIMALWARE;
    else if (protection == SAR_PPL_AVAILABLE && g_sar.posture.ppl_available == SAR_PPL_NONE)
        g_sar.posture.ppl_available = SAR_PPL_AVAILABLE;

    *ConnectionCookie = comm;
    return STATUS_SUCCESS;

reject:
    return STATUS_ACCESS_DENIED;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarCommDisconnectNotify(_In_opt_ PVOID ConnectionCookie)
{
    PSAR_COMM comm = (PSAR_COMM)ConnectionCookie;

    if (comm == NULL)
        return;

    if (comm->client_port != NULL) {
        FltCloseClientPort(comm->filter, &comm->client_port);
        comm->client_port = NULL;
    }
    sar_handshake_timeout(&comm->handshake);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS SarHandleConnectResponse(_Inout_ PSAR_COMM Comm,
                                         _In_ const semantics_ar_connect_response_t *Response)
{
    sar_hs_result_t hs;

    if (Response->sig_length > SEMANTICS_AR_HS_SIG_MAX)
        return STATUS_INVALID_PARAMETER;

    hs = sar_handshake_verify_response(&Comm->handshake, Response->signature, Response->sig_length);
    if (hs != SAR_HS_RESULT_OK) {
        InterlockedIncrement64(&Comm->tamper_counter);
        return STATUS_REQUEST_NOT_ACCEPTED;
    }

    hs = sar_handshake_check_version(&Comm->handshake, Response->header.protocol_version);
    if (hs != SAR_HS_RESULT_OK) {
        InterlockedIncrement64(&Comm->tamper_counter);
        return STATUS_REVISION_MISMATCH;
    }

    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS SarHandleGetStatus(_Inout_ PSAR_COMM Comm,
                                   _In_ const semantics_ar_get_status_t *Request,
                                   _Out_writes_bytes_(OutputBufferLength) PVOID OutputBuffer,
                                   _In_ ULONG OutputBufferLength,
                                   _Out_ PULONG ReturnLength)
{
    semantics_ar_status_reply_t reply;

    UNREFERENCED_PARAMETER(Request);

    if (!sar_handshake_authenticated(&Comm->handshake)) {
        InterlockedIncrement64(&Comm->tamper_counter);
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (OutputBuffer == NULL || OutputBufferLength < sizeof(reply))
        return STATUS_BUFFER_TOO_SMALL;

    RtlZeroMemory(&reply, sizeof(reply));
    reply.header.protocol_version = SEMANTICS_AR_PROTOCOL_VERSION;
    reply.header.message_type = SEMANTICS_AR_MSG_STATUS_REPLY;
    reply.header.message_length = (uint32_t)sizeof(reply);
    reply.result = 0;
    reply.protocol_version = SEMANTICS_AR_PROTOCOL_VERSION;
    reply.mode = SarStateModeGet(g_sar_state);
    reply.captured_key_count = (uint64_t)g_sar_state->captured_key_count;

    {
        SAR_PRESERVE_STATS stats;
        SarPreserveStats(g_sar.preserve, &stats);
        reply.preserve_capacity_bytes = stats.capacity_bytes;
        reply.preserve_used_bytes = stats.used_bytes;
        reply.preserve_retention_100ns = stats.retention_100ns;
        reply.preserve_oldest_protected_time = stats.oldest_protected_time;
        reply.preserve_protected_count = stats.protected_count;
        reply.preserve_probation_count = stats.probation_count;
    }

    reply.capture_inflight = (uint64_t)SarCaptureInflight(g_sar.capture);

    RtlCopyMemory(OutputBuffer, &reply, sizeof(reply));
    *ReturnLength = (ULONG)sizeof(reply);
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS SarHandleProcessQuery(_Inout_ PSAR_COMM Comm,
                                      _In_ const semantics_ar_process_query_t *Request,
                                      _Out_writes_bytes_(OutputBufferLength) PVOID OutputBuffer,
                                      _In_ ULONG OutputBufferLength,
                                      _Out_ PULONG ReturnLength)
{
    semantics_ar_process_reply_t reply;
    UINT64 sk = 0;
    sar_id_state_t st = SAR_IDSTATE_OBSERVE_PENDING;

    if (!sar_handshake_authenticated(&Comm->handshake)) {
        InterlockedIncrement64(&Comm->tamper_counter);
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (OutputBuffer == NULL || OutputBufferLength < sizeof(reply))
        return STATUS_BUFFER_TOO_SMALL;

    RtlZeroMemory(&reply, sizeof(reply));
    reply.header.protocol_version = SEMANTICS_AR_PROTOCOL_VERSION;
    reply.header.message_type = SEMANTICS_AR_MSG_PROCESS_REPLY;
    reply.header.message_length = (uint32_t)sizeof(reply);
    reply.valid = SarStateIdentityQuery(g_sar_state, (HANDLE)(ULONG_PTR)Request->pid, &sk, &st)
                      ? 1u : 0u;
    reply.start_key = sk;
    reply.id_state = (uint32_t)st;

    RtlCopyMemory(OutputBuffer, &reply, sizeof(reply));
    *ReturnLength = (ULONG)sizeof(reply);
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS SarHandleRecoveryExec(_Inout_ PSAR_COMM Comm,
                                      _In_ const semantics_ar_recovery_exec_t *Request,
                                      _Out_writes_bytes_(OutputBufferLength) PVOID OutputBuffer,
                                      _In_ ULONG OutputBufferLength,
                                      _Out_ PULONG ReturnLength)
{
    semantics_ar_recovery_exec_t req;
    semantics_ar_recovery_result_t reply;
    UINT64 bytes = 0;

    if (!sar_handshake_authenticated(&Comm->handshake)) {
        InterlockedIncrement64(&Comm->tamper_counter);
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (OutputBuffer == NULL || OutputBufferLength < sizeof(reply))
        return STATUS_BUFFER_TOO_SMALL;

    RtlCopyMemory(&req, Request, sizeof(req));

    RtlZeroMemory(&reply, sizeof(reply));
    reply.header.protocol_version = SEMANTICS_AR_PROTOCOL_VERSION;
    reply.header.message_type = SEMANTICS_AR_MSG_RECOVERY_RESULT;
    reply.header.message_length = (uint32_t)sizeof(reply);
    reply.status = SarRecoveryExecute(&req, &bytes);
    reply.bytes_recovered = bytes;

    RtlSecureZeroMemory(&req, sizeof(req));

    RtlCopyMemory(OutputBuffer, &reply, sizeof(reply));
    *ReturnLength = (ULONG)sizeof(reply);
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS SarHandleCatalogQuery(_Inout_ PSAR_COMM Comm,
                                      _In_ const semantics_ar_catalog_query_t *Request,
                                      _Out_writes_bytes_(OutputBufferLength) PVOID OutputBuffer,
                                      _In_ ULONG OutputBufferLength,
                                      _Out_ PULONG ReturnLength)
{
    semantics_ar_catalog_reply_t reply;
    ULONG64 total = 0;

    if (!sar_handshake_authenticated(&Comm->handshake)) {
        InterlockedIncrement64(&Comm->tamper_counter);
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (OutputBuffer == NULL || OutputBufferLength < sizeof(reply))
        return STATUS_BUFFER_TOO_SMALL;

    RtlZeroMemory(&reply, sizeof(reply));
    reply.header.protocol_version = SEMANTICS_AR_PROTOCOL_VERSION;
    reply.header.message_type = SEMANTICS_AR_MSG_CATALOG_REPLY;
    reply.header.message_length = (uint32_t)sizeof(reply);
    reply.result = 0;
    reply.index = Request->index;

    if (g_sar.keystore != NULL && SarKeystoreReady(g_sar.keystore)) {
        reply.valid = SarKeystoreProject(g_sar.keystore, Request->index,
                                         &reply.entry, &total) ? 1u : 0u;
        reply.total = (uint32_t)total;
    }

    RtlCopyMemory(OutputBuffer, &reply, sizeof(reply));
    *ReturnLength = (ULONG)sizeof(reply);
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS SarHandlePreserveQuery(_Inout_ PSAR_COMM Comm,
                                       _In_ const semantics_ar_preserve_query_t *Request,
                                       _Out_writes_bytes_(OutputBufferLength) PVOID OutputBuffer,
                                       _In_ ULONG OutputBufferLength,
                                       _Out_ PULONG ReturnLength)
{
    semantics_ar_preserve_reply_t reply;
    ULONG64 total = 0;

    if (!sar_handshake_authenticated(&Comm->handshake)) {
        InterlockedIncrement64(&Comm->tamper_counter);
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (OutputBuffer == NULL || OutputBufferLength < sizeof(reply))
        return STATUS_BUFFER_TOO_SMALL;

    RtlZeroMemory(&reply, sizeof(reply));
    reply.header.protocol_version = SEMANTICS_AR_PROTOCOL_VERSION;
    reply.header.message_type = SEMANTICS_AR_MSG_PRESERVE_REPLY;
    reply.header.message_length = (uint32_t)sizeof(reply);
    reply.result = 0;
    reply.index = Request->index;

    if (g_sar.preserve != NULL && SarPreserveReady(g_sar.preserve)) {
        reply.valid = SarPreserveProject(g_sar.preserve, Request->index,
                                         &reply.entry, &total) ? 1u : 0u;
        reply.total = (uint32_t)total;
    }

    RtlCopyMemory(OutputBuffer, &reply, sizeof(reply));
    *ReturnLength = (ULONG)sizeof(reply);
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS SarHandlePreserveRecover(_Inout_ PSAR_COMM Comm,
                                         _In_ const semantics_ar_preserve_recover_t *Request,
                                         _Out_writes_bytes_(OutputBufferLength) PVOID OutputBuffer,
                                         _In_ ULONG OutputBufferLength,
                                         _Out_ PULONG ReturnLength)
{
    semantics_ar_preserve_recover_t req;
    semantics_ar_recovery_result_t reply;
    UINT64 bytes = 0;

    if (!sar_handshake_authenticated(&Comm->handshake)) {
        InterlockedIncrement64(&Comm->tamper_counter);
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (OutputBuffer == NULL || OutputBufferLength < sizeof(reply))
        return STATUS_BUFFER_TOO_SMALL;

    RtlCopyMemory(&req, Request, sizeof(req));

    RtlZeroMemory(&reply, sizeof(reply));
    reply.header.protocol_version = SEMANTICS_AR_PROTOCOL_VERSION;
    reply.header.message_type = SEMANTICS_AR_MSG_PRESERVE_RESULT;
    reply.header.message_length = (uint32_t)sizeof(reply);
    reply.status = SarPreserveRecoveryExecute(&req, &bytes);
    reply.bytes_recovered = bytes;

    RtlSecureZeroMemory(&req, sizeof(req));

    RtlCopyMemory(OutputBuffer, &reply, sizeof(reply));
    *ReturnLength = (ULONG)sizeof(reply);
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS SarHandleEventsQuery(_Inout_ PSAR_COMM Comm,
                                     _In_ const semantics_ar_events_query_t *Request,
                                     _Out_writes_bytes_(OutputBufferLength) PVOID OutputBuffer,
                                     _In_ ULONG OutputBufferLength,
                                     _Out_ PULONG ReturnLength)
{
    semantics_ar_events_reply_t reply;
    SAR_EVENT_RECORD record;
    BOOLEAN valid = FALSE;
    BOOLEAN gap = FALSE;

    if (!sar_handshake_authenticated(&Comm->handshake)) {
        InterlockedIncrement64(&Comm->tamper_counter);
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (OutputBuffer == NULL || OutputBufferLength < sizeof(reply))
        return STATUS_BUFFER_TOO_SMALL;

    RtlZeroMemory(&reply, sizeof(reply));
    reply.header.protocol_version = SEMANTICS_AR_PROTOCOL_VERSION;
    reply.header.message_type = SEMANTICS_AR_MSG_EVENTS_REPLY;
    reply.header.message_length = (uint32_t)sizeof(reply);
    reply.result = 0;

    SarEventLogQuery(g_sar.eventlog, Request->generation, Request->sequence,
                     &record, &valid, &gap);
    reply.valid = valid ? 1u : 0u;
    reply.gap = gap ? 1u : 0u;
    if (valid) {
        reply.event_class = record.event_class;
        reply.generation = record.generation;
        reply.sequence = record.sequence;
        reply.timestamp = record.timestamp_100ns;
        reply.actor_start_key = record.actor_start_key;
    }

    RtlCopyMemory(OutputBuffer, &reply, sizeof(reply));
    *ReturnLength = (ULONG)sizeof(reply);
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarCommMessageNotify(_In_opt_ PVOID PortCookie,
                              _In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
                              _In_ ULONG InputBufferLength,
                              _Out_writes_bytes_to_opt_(OutputBufferLength, *ReturnOutputBufferLength) PVOID OutputBuffer,
                              _In_ ULONG OutputBufferLength,
                              _Out_ PULONG ReturnOutputBufferLength)
{
    PSAR_COMM comm = (PSAR_COMM)PortCookie;
    sar_msg_status_t mv;
    uint32_t type = 0;
    NTSTATUS status = STATUS_INVALID_PARAMETER;

    *ReturnOutputBufferLength = 0;

    if (comm == NULL || InputBuffer == NULL)
        return STATUS_INVALID_PARAMETER;

    __try {

    mv = sar_msg_validate((const uint8_t *)InputBuffer, InputBufferLength, &type);
    if (mv != SAR_MSG_OK) {
        InterlockedIncrement64(&comm->tamper_counter);
        return STATUS_INVALID_PARAMETER;
    }

    switch (type) {
    case SEMANTICS_AR_MSG_CONNECT_RESPONSE:
        status = SarHandleConnectResponse(comm, (const semantics_ar_connect_response_t *)InputBuffer);
        break;
    case SEMANTICS_AR_MSG_GET_STATUS:
        status = SarHandleGetStatus(comm, (const semantics_ar_get_status_t *)InputBuffer,
                                    OutputBuffer, OutputBufferLength, ReturnOutputBufferLength);
        break;
    case SEMANTICS_AR_MSG_RECOVERY_EXEC:
        status = SarHandleRecoveryExec(comm, (const semantics_ar_recovery_exec_t *)InputBuffer,
                                       OutputBuffer, OutputBufferLength, ReturnOutputBufferLength);
        break;
    case SEMANTICS_AR_MSG_CATALOG_QUERY:
        status = SarHandleCatalogQuery(comm, (const semantics_ar_catalog_query_t *)InputBuffer,
                                       OutputBuffer, OutputBufferLength, ReturnOutputBufferLength);
        break;
    case SEMANTICS_AR_MSG_PRESERVE_QUERY:
        status = SarHandlePreserveQuery(comm, (const semantics_ar_preserve_query_t *)InputBuffer,
                                        OutputBuffer, OutputBufferLength, ReturnOutputBufferLength);
        break;
    case SEMANTICS_AR_MSG_PRESERVE_RECOVER:
        status = SarHandlePreserveRecover(comm, (const semantics_ar_preserve_recover_t *)InputBuffer,
                                          OutputBuffer, OutputBufferLength, ReturnOutputBufferLength);
        break;
    case SEMANTICS_AR_MSG_EVENTS_QUERY:
        status = SarHandleEventsQuery(comm, (const semantics_ar_events_query_t *)InputBuffer,
                                      OutputBuffer, OutputBufferLength, ReturnOutputBufferLength);
        break;
    case SEMANTICS_AR_MSG_SET_BUDGET:
        if (!sar_handshake_authenticated(&comm->handshake)) {
            InterlockedIncrement64(&comm->tamper_counter);
            status = STATUS_INVALID_DEVICE_STATE;
        } else {
            const semantics_ar_set_budget_t *b = (const semantics_ar_set_budget_t *)InputBuffer;
            SarPreserveSetBudget(g_sar.preserve, b->retention_100ns, b->capacity_bytes);
            status = STATUS_SUCCESS;
        }
        break;
    case SEMANTICS_AR_MSG_SET_MODE:
        if (!sar_handshake_authenticated(&comm->handshake)) {
            InterlockedIncrement64(&comm->tamper_counter);
            status = STATUS_INVALID_DEVICE_STATE;
        } else {
            const semantics_ar_set_mode_t *m = (const semantics_ar_set_mode_t *)InputBuffer;
            status = SarStateModeSet(g_sar_state, m->mode) ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
        }
        break;
    case SEMANTICS_AR_MSG_WHITELIST_ADD:
    case SEMANTICS_AR_MSG_WHITELIST_REMOVE:
        if (!sar_handshake_authenticated(&comm->handshake)) {
            InterlockedIncrement64(&comm->tamper_counter);
            status = STATUS_INVALID_DEVICE_STATE;
        } else {
            const semantics_ar_whitelist_control_t *w = (const semantics_ar_whitelist_control_t *)InputBuffer;
            sar_identity_t id;
            RtlZeroMemory(&id, sizeof(id));
            RtlCopyMemory(id.image_path, w->image_path, sizeof(id.image_path));
            RtlCopyMemory(id.cert_subject, w->cert_subject, sizeof(id.cert_subject));
            RtlCopyMemory(id.content_hash, w->content_hash, sizeof(id.content_hash));
            if (type == SEMANTICS_AR_MSG_WHITELIST_ADD)
                status = (SarStateWhitelistAdd(g_sar_state, &id) == SAR_WL_OK) ?
                          STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
            else
                status = (SarStateWhitelistRemove(g_sar_state, &id) == SAR_WL_OK) ?
                          STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
        }
        break;
    case SEMANTICS_AR_MSG_PROCESS_QUERY:
        status = SarHandleProcessQuery(comm, (const semantics_ar_process_query_t *)InputBuffer,
                                       OutputBuffer, OutputBufferLength, ReturnOutputBufferLength);
        break;
    case SEMANTICS_AR_MSG_IDENTITY_VERDICT:
        if (!sar_handshake_authenticated(&comm->handshake)) {
            InterlockedIncrement64(&comm->tamper_counter);
            status = STATUS_INVALID_DEVICE_STATE;
        } else {
            const semantics_ar_identity_verdict_t *v = (const semantics_ar_identity_verdict_t *)InputBuffer;
            sar_identity_t id;
            RtlZeroMemory(&id, sizeof(id));
            RtlCopyMemory(id.image_path, v->image_path, sizeof(id.image_path));
            RtlCopyMemory(id.cert_subject, v->cert_subject, sizeof(id.cert_subject));
            RtlCopyMemory(id.content_hash, v->content_hash, sizeof(id.content_hash));
            status = SarStateIdentityApplyVerdict(g_sar_state, (HANDLE)(ULONG_PTR)v->pid,
                                                  v->start_key, &id) ?
                      STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
        }
        break;
    default:
        InterlockedIncrement64(&comm->tamper_counter);
        status = STATUS_INVALID_PARAMETER;
        break;
    }

    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedIncrement64(&comm->tamper_counter);
        *ReturnOutputBufferLength = 0;
        return STATUS_INVALID_PARAMETER;
    }

    return status;
}

#define SAR_POOL_TAG_PORTSEC 'sPrS'

_IRQL_requires_max_(PASSIVE_LEVEL)
static VOID SarFreePortSecurity(_In_ PSECURITY_DESCRIPTOR SecurityDescriptor)
{
    ExFreePoolWithTag(SecurityDescriptor, SAR_POOL_TAG_PORTSEC);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS SarBuildPortSecurity(_Outptr_ PSECURITY_DESCRIPTOR *SecurityDescriptor)
{
    SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;
    ULONG acl_length;
    PSID system_sid;
    PACL acl;
    PSECURITY_DESCRIPTOR sd;
    PUCHAR base;
    NTSTATUS status;

    *SecurityDescriptor = NULL;

    acl_length = (ULONG)(sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) - sizeof(ULONG) + sizeof(SID));

    base = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                   sizeof(SECURITY_DESCRIPTOR) + sizeof(SID) + acl_length,
                                   SAR_POOL_TAG_PORTSEC);
    if (base == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    sd = (PSECURITY_DESCRIPTOR)base;
    system_sid = (PSID)(base + sizeof(SECURITY_DESCRIPTOR));
    acl = (PACL)(base + sizeof(SECURITY_DESCRIPTOR) + sizeof(SID));

    status = RtlInitializeSid(system_sid, &nt_authority, 1);
    if (NT_SUCCESS(status))
        *RtlSubAuthoritySid(system_sid, 0) = SECURITY_LOCAL_SYSTEM_RID;

    if (NT_SUCCESS(status))
        status = RtlCreateSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION);

    if (NT_SUCCESS(status))
        status = RtlCreateAcl(acl, acl_length, ACL_REVISION);

    if (NT_SUCCESS(status))
        status = RtlAddAccessAllowedAce(acl, ACL_REVISION, FLT_PORT_ALL_ACCESS, system_sid);

    if (NT_SUCCESS(status))
        status = RtlSetOwnerSecurityDescriptor(sd, system_sid, FALSE);

    if (NT_SUCCESS(status))
        status = RtlSetDaclSecurityDescriptor(sd, TRUE, acl, FALSE);

    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(base, SAR_POOL_TAG_PORTSEC);
        return status;
    }

    *SecurityDescriptor = sd;
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarCommPortCreate(_In_ PFLT_FILTER Filter, _Outptr_ struct _SAR_COMM **Comm)
{
    PSAR_COMM comm;
    PSECURITY_DESCRIPTOR sd = NULL;
    OBJECT_ATTRIBUTES attr;
    UNICODE_STRING name;
    NTSTATUS status;

    *Comm = NULL;

    comm = (PSAR_COMM)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(SAR_COMM), SAR_POOL_TAG_COMM);
    if (comm == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    comm->filter = Filter;
    comm->server_port = NULL;
    comm->client_port = NULL;
    comm->tamper_counter = 0;
    sar_handshake_init(&comm->handshake, SEMANTICS_AR_PROTOCOL_VERSION, SarHandshakeVerify, NULL);

    status = SarBuildPortSecurity(&sd);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(comm, SAR_POOL_TAG_COMM);
        return status;
    }

    RtlInitUnicodeString(&name, SAR_COMM_PORT_NAME);
    InitializeObjectAttributes(&attr, &name,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               NULL, sd);

    status = FltCreateCommunicationPort(Filter, &comm->server_port, &attr, comm,
                                        SarCommConnectNotify, SarCommDisconnectNotify,
                                        SarCommMessageNotify, 1);
    SarFreePortSecurity(sd);

    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(comm, SAR_POOL_TAG_COMM);
        return status;
    }

    *Comm = comm;
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarCommPortClose(_Inout_ struct _SAR_COMM *Comm)
{
    if (Comm == NULL)
        return;

    if (Comm->client_port != NULL) {
        FltCloseClientPort(Comm->filter, &Comm->client_port);
        Comm->client_port = NULL;
    }
    if (Comm->server_port != NULL) {
        FltCloseCommunicationPort(Comm->server_port);
        Comm->server_port = NULL;
    }
    ExFreePoolWithTag(Comm, SAR_POOL_TAG_COMM);
}
