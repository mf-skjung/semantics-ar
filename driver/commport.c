#include "commport.h"
#include "state.h"
#include "ntsystem.h"

#include <bcrypt.h>

extern PSAR_STATE g_sar_state;

static const WCHAR g_sar_service_image_allow[] = L"\\semantics_ar_service.exe";

static const UCHAR g_sar_service_public_key[] = {
    0x45, 0x43, 0x53, 0x31, 0x20, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

typedef NTSTATUS (NTAPI *SAR_BCRYPT_GENRANDOM)(BCRYPT_ALG_HANDLE, PUCHAR, ULONG, ULONG);
typedef NTSTATUS (NTAPI *SAR_BCRYPT_OPEN_ALG)(BCRYPT_ALG_HANDLE *, LPCWSTR, LPCWSTR, ULONG);
typedef NTSTATUS (NTAPI *SAR_BCRYPT_CLOSE_ALG)(BCRYPT_ALG_HANDLE, ULONG);
typedef NTSTATUS (NTAPI *SAR_BCRYPT_IMPORT_KEYPAIR)(BCRYPT_ALG_HANDLE, BCRYPT_KEY_HANDLE, LPCWSTR,
                                                    BCRYPT_KEY_HANDLE *, PUCHAR, ULONG, ULONG);
typedef NTSTATUS (NTAPI *SAR_BCRYPT_DESTROY_KEY)(BCRYPT_KEY_HANDLE);
typedef NTSTATUS (NTAPI *SAR_BCRYPT_VERIFY_SIGNATURE)(BCRYPT_KEY_HANDLE, VOID *, PUCHAR, ULONG,
                                                      PUCHAR, ULONG, ULONG);

typedef struct _SAR_BCRYPT_FNS {
    SAR_BCRYPT_GENRANDOM gen_random;
    SAR_BCRYPT_OPEN_ALG open_alg;
    SAR_BCRYPT_CLOSE_ALG close_alg;
    SAR_BCRYPT_IMPORT_KEYPAIR import_keypair;
    SAR_BCRYPT_DESTROY_KEY destroy_key;
    SAR_BCRYPT_VERIFY_SIGNATURE verify_signature;
    BOOLEAN resolved;
} SAR_BCRYPT_FNS;

static SAR_BCRYPT_FNS g_sar_bcrypt;

_IRQL_requires_max_(PASSIVE_LEVEL)
static PVOID SarResolveSystemRoutine(_In_ PCWSTR Name)
{
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, Name);
    return MmGetSystemRoutineAddress(&name);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static BOOLEAN SarBcryptResolve(VOID)
{
    if (g_sar_bcrypt.resolved)
        return TRUE;

    g_sar_bcrypt.gen_random = (SAR_BCRYPT_GENRANDOM)SarResolveSystemRoutine(L"BCryptGenRandom");
    g_sar_bcrypt.open_alg = (SAR_BCRYPT_OPEN_ALG)SarResolveSystemRoutine(L"BCryptOpenAlgorithmProvider");
    g_sar_bcrypt.close_alg = (SAR_BCRYPT_CLOSE_ALG)SarResolveSystemRoutine(L"BCryptCloseAlgorithmProvider");
    g_sar_bcrypt.import_keypair = (SAR_BCRYPT_IMPORT_KEYPAIR)SarResolveSystemRoutine(L"BCryptImportKeyPair");
    g_sar_bcrypt.destroy_key = (SAR_BCRYPT_DESTROY_KEY)SarResolveSystemRoutine(L"BCryptDestroyKey");
    g_sar_bcrypt.verify_signature = (SAR_BCRYPT_VERIFY_SIGNATURE)SarResolveSystemRoutine(L"BCryptVerifySignature");

    if (g_sar_bcrypt.gen_random == NULL || g_sar_bcrypt.open_alg == NULL ||
        g_sar_bcrypt.close_alg == NULL || g_sar_bcrypt.import_keypair == NULL ||
        g_sar_bcrypt.destroy_key == NULL || g_sar_bcrypt.verify_signature == NULL)
        return FALSE;

    g_sar_bcrypt.resolved = TRUE;
    return TRUE;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static int SarHandshakeVerify(const uint8_t *nonce, uint32_t nonce_len,
                              const uint8_t *signature, uint32_t sig_len, void *ctx)
{
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_KEY_HANDLE key = NULL;
    NTSTATUS status;
    int result = 0;

    UNREFERENCED_PARAMETER(ctx);

    if (!g_sar_bcrypt.resolved)
        return 0;
    if (nonce == NULL || signature == NULL)
        return 0;

    status = g_sar_bcrypt.open_alg(&alg, BCRYPT_ECDSA_P256_ALGORITHM, NULL, 0);
    if (!NT_SUCCESS(status))
        return 0;

    status = g_sar_bcrypt.import_keypair(alg, NULL, BCRYPT_ECCPUBLIC_BLOB, &key,
                                         (PUCHAR)g_sar_service_public_key,
                                         (ULONG)sizeof(g_sar_service_public_key), 0);
    if (NT_SUCCESS(status)) {
        status = g_sar_bcrypt.verify_signature(key, NULL,
                                               (PUCHAR)nonce, nonce_len,
                                               (PUCHAR)signature, sig_len, 0);
        if (NT_SUCCESS(status))
            result = 1;
        g_sar_bcrypt.destroy_key(key);
    }

    g_sar_bcrypt.close_alg(alg, 0);
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

    status = g_sar_bcrypt.gen_random(NULL, nonce, SAR_HS_NONCE_SIZE,
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

    if (!SarBcryptResolve())
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

    status = SarIssueChallenge(comm);
    if (!NT_SUCCESS(status)) {
        comm->client_port = NULL;
        goto reject;
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
    sar_hs_result_t hs;

    UNREFERENCED_PARAMETER(Request);

    hs = sar_handshake_check_version(&Comm->handshake, SEMANTICS_AR_PROTOCOL_VERSION);
    if (hs != SAR_HS_RESULT_OK)
        return STATUS_REVISION_MISMATCH;

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
    NTSTATUS status;

    *ReturnOutputBufferLength = 0;

    if (comm == NULL || InputBuffer == NULL)
        return STATUS_INVALID_PARAMETER;

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
    default:
        InterlockedIncrement64(&comm->tamper_counter);
        status = STATUS_INVALID_PARAMETER;
        break;
    }

    return status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS SarBuildPortSecurity(_Outptr_ PSECURITY_DESCRIPTOR *SecurityDescriptor)
{
    return FltBuildDefaultSecurityDescriptor(SecurityDescriptor, FLT_PORT_ALL_ACCESS);
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
    FltFreeSecurityDescriptor(sd);

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
