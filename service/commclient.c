#include "commclient.h"

#include <fltUser.h>
#include <ncrypt.h>
#include <string.h>

#include "sar_control.h"

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

#define SAR_COMM_RECV_BUFFER 2048u

typedef union {
    semantics_ar_connect_challenge_t challenge;
    semantics_ar_connect_response_t  connect_response;
    semantics_ar_status_reply_t      status_reply;
    semantics_ar_catalog_reply_t     catalog_reply;
    semantics_ar_preserve_reply_t    preserve_reply;
    semantics_ar_events_reply_t      events_reply;
    semantics_ar_process_reply_t     process_reply;
    semantics_ar_identity_verdict_t  identity_verdict;
    semantics_ar_verdict_notify_t    verdict_notify;
    semantics_ar_whitelist_control_t whitelist_control;
    semantics_ar_whitelist_reply_t   whitelist_reply;
    semantics_ar_recovery_exec_t     recovery_exec;
    semantics_ar_recovery_result_t   recovery_result;
} sar_comm_any_msg_t;

C_ASSERT(sizeof(sar_comm_any_msg_t) <= SAR_COMM_RECV_BUFFER);

typedef struct {
    FILTER_MESSAGE_HEADER fltHeader;
    uint8_t               payload[SAR_COMM_RECV_BUFFER];
} sar_filter_message_t;

typedef struct {
    FILTER_REPLY_HEADER fltHeader;
    uint8_t             payload[SAR_COMM_RECV_BUFFER];
} sar_filter_reply_t;

static void sar_comm_init_header(semantics_ar_msg_header_t *h, uint32_t type,
                                 uint32_t length)
{
    h->protocol_version = SEMANTICS_AR_PROTOCOL_VERSION;
    h->message_type = type;
    h->message_length = length;
}

sar_comm_status_t sar_comm_open_signing_key(sar_comm_client_t *client,
                                            const wchar_t *key_path)
{
    NCRYPT_PROV_HANDLE provider = 0;
    NCRYPT_KEY_HANDLE  key = 0;
    SECURITY_STATUS    st;

    if (!client || !key_path)
        return SAR_COMM_ERR_KEY;

    st = NCryptOpenStorageProvider(&provider, MS_KEY_STORAGE_PROVIDER, 0);
    if (st != ERROR_SUCCESS)
        return SAR_COMM_ERR_KEY;

    st = NCryptOpenKey(provider, &key, key_path, 0,
                       NCRYPT_MACHINE_KEY_FLAG | NCRYPT_SILENT_FLAG);
    if (st != ERROR_SUCCESS) {
        NCryptFreeObject(provider);
        return SAR_COMM_ERR_KEY;
    }

    client->sign_provider = (void *)provider;
    client->sign_key = (void *)key;
    return SAR_COMM_OK;
}

sar_comm_status_t sar_comm_connect(sar_comm_client_t *client,
                                   const wchar_t *port_name)
{
    HRESULT hr;
    HANDLE  port = INVALID_HANDLE_VALUE;

    if (!client || !port_name)
        return SAR_COMM_ERR_CONNECT;

    hr = FilterConnectCommunicationPort(port_name, 0, NULL, 0, NULL, &port);
    if (FAILED(hr) || port == INVALID_HANDLE_VALUE)
        return SAR_COMM_ERR_CONNECT;

    client->stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!client->stop_event) {
        CloseHandle(port);
        return SAR_COMM_ERR_CONNECT;
    }

    client->port = port;
    InterlockedExchange(&client->running, 1);
    return SAR_COMM_OK;
}

static sar_comm_status_t sar_comm_recv_message(sar_comm_client_t *client,
                                               uint8_t *buf, uint32_t capacity,
                                               uint32_t *out_type,
                                               uint32_t *out_length)
{
    sar_filter_message_t msg;
    HRESULT    hr;
    OVERLAPPED ov;
    HANDLE     ev;
    DWORD      bytes = 0;
    BOOL       completed = FALSE;
    uint32_t   type;

    if (capacity > SAR_COMM_RECV_BUFFER)
        capacity = SAR_COMM_RECV_BUFFER;

    ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ev)
        return SAR_COMM_ERR_TRANSPORT;

    memset(&msg, 0, sizeof(msg));
    memset(&ov, 0, sizeof(ov));
    ov.hEvent = ev;

    hr = FilterGetMessage(client->port, &msg.fltHeader,
                          FIELD_OFFSET(sar_filter_message_t, payload) + capacity, &ov);
    if (hr == HRESULT_FROM_WIN32(ERROR_IO_PENDING)) {
        HANDLE waits[2];
        DWORD  w;

        waits[0] = ev;
        waits[1] = client->stop_event;
        w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (w == WAIT_OBJECT_0) {
            completed = GetOverlappedResult(client->port, &ov, &bytes, FALSE);
        } else if (w == WAIT_OBJECT_0 + 1) {
            CancelIoEx(client->port, &ov);
            WaitForSingleObject(ev, INFINITE);
            completed = GetOverlappedResult(client->port, &ov, &bytes, FALSE);
            if (!completed) {
                CloseHandle(ev);
                return SAR_COMM_ERR_STOPPED;
            }
        } else {
            CloseHandle(ev);
            return SAR_COMM_ERR_TRANSPORT;
        }
    } else if (SUCCEEDED(hr)) {
        completed = TRUE;
    }

    CloseHandle(ev);
    if (!completed)
        return SAR_COMM_ERR_TRANSPORT;

    {
        semantics_ar_msg_header_t hdr;
        uint32_t mlen;

        if (capacity < sizeof(hdr))
            return SAR_COMM_ERR_PROTOCOL;
        memcpy(&hdr, msg.payload, sizeof(hdr));
        mlen = hdr.message_length;
        if (mlen > capacity)
            return SAR_COMM_ERR_PROTOCOL;
        if (sar_msg_validate(msg.payload, mlen, &type) != SAR_MSG_OK)
            return SAR_COMM_ERR_PROTOCOL;
    }

    {
        uint32_t declared = sar_msg_expected_length(type);
        if (declared == 0 || declared > capacity)
            return SAR_COMM_ERR_PROTOCOL;
        memcpy(buf, msg.payload, declared);
        *out_type = type;
        *out_length = declared;
    }
    return SAR_COMM_OK;
}

sar_comm_status_t sar_comm_send(sar_comm_client_t *client,
                                const void *message, uint32_t length)
{
    HRESULT hr;
    DWORD   returned = 0;

    if (!client || client->port == INVALID_HANDLE_VALUE)
        return SAR_COMM_ERR_TRANSPORT;

    hr = FilterSendMessage(client->port, (LPVOID)message, length,
                           NULL, 0, &returned);
    if (FAILED(hr))
        return SAR_COMM_ERR_TRANSPORT;
    return SAR_COMM_OK;
}

sar_comm_status_t sar_comm_send_recv(sar_comm_client_t *client,
                                     const void *message, uint32_t length,
                                     void *reply, uint32_t reply_capacity,
                                     uint32_t expected_type)
{
    HRESULT  hr;
    DWORD    returned = 0;
    uint32_t type;

    if (!client || client->port == INVALID_HANDLE_VALUE)
        return SAR_COMM_ERR_TRANSPORT;
    if (reply_capacity > SAR_COMM_RECV_BUFFER)
        return SAR_COMM_ERR_PROTOCOL;

    {
        uint8_t scratch[SAR_COMM_RECV_BUFFER];
        memset(scratch, 0, sizeof(scratch));
        hr = FilterSendMessage(client->port, (LPVOID)message, length,
                               scratch, reply_capacity, &returned);
        if (FAILED(hr))
            return SAR_COMM_ERR_TRANSPORT;

        if (sar_msg_validate(scratch, returned, &type) != SAR_MSG_OK)
            return SAR_COMM_ERR_PROTOCOL;
        if (type != expected_type)
            return SAR_COMM_ERR_PROTOCOL;

        {
            uint32_t declared = sar_msg_expected_length(type);
            if (declared == 0 || declared > reply_capacity)
                return SAR_COMM_ERR_PROTOCOL;
            memcpy(reply, scratch, declared);
        }
    }
    return SAR_COMM_OK;
}

static sar_comm_status_t sar_comm_sign_nonce(sar_comm_client_t *client,
                                             const uint8_t *nonce,
                                             uint32_t nonce_len,
                                             uint8_t *sig, uint32_t sig_cap,
                                             uint32_t *out_sig_len)
{
    SECURITY_STATUS ss;
    DWORD produced = 0;

    ss = NCryptSignHash((NCRYPT_KEY_HANDLE)client->sign_key, NULL,
                        (PBYTE)nonce, nonce_len,
                        sig, sig_cap, &produced, NCRYPT_SILENT_FLAG);
    if (ss != ERROR_SUCCESS || produced == 0 || produced > sig_cap)
        return SAR_COMM_ERR_HANDSHAKE;

    *out_sig_len = (uint32_t)produced;
    return SAR_COMM_OK;
}

sar_comm_status_t sar_comm_handshake(sar_comm_client_t *client)
{
    uint8_t  rxbuf[SAR_COMM_RECV_BUFFER];
    uint32_t rxtype = 0;
    uint32_t rxlen = 0;
    sar_comm_status_t cs;

    if (!client || client->port == INVALID_HANDLE_VALUE || !client->sign_key)
        return SAR_COMM_ERR_HANDSHAKE;

    cs = sar_comm_recv_message(client, rxbuf, sizeof(rxbuf), &rxtype, &rxlen);
    if (cs != SAR_COMM_OK)
        return cs;
    if (rxtype != SEMANTICS_AR_MSG_CONNECT_CHALLENGE)
        return SAR_COMM_ERR_HANDSHAKE;

    {
        const semantics_ar_connect_challenge_t *chal =
            (const semantics_ar_connect_challenge_t *)rxbuf;
        semantics_ar_connect_response_t resp;
        uint32_t sig_len = 0;

        memset(&resp, 0, sizeof(resp));
        cs = sar_comm_sign_nonce(client, chal->nonce,
                                 SEMANTICS_AR_HS_NONCE_SIZE,
                                 resp.signature, SEMANTICS_AR_HS_SIG_MAX,
                                 &sig_len);
        if (cs != SAR_COMM_OK)
            return cs;

        resp.sig_length = sig_len;
        sar_comm_init_header(&resp.header, SEMANTICS_AR_MSG_CONNECT_RESPONSE,
                             (uint32_t)sizeof(resp));
        cs = sar_comm_send(client, &resp, (uint32_t)sizeof(resp));
        if (cs != SAR_COMM_OK)
            return cs;
    }

    {
        semantics_ar_status_reply_t reply;

        cs = sar_comm_query_status(client, &reply);
        if (cs != SAR_COMM_OK)
            return cs;

        client->mode = reply.mode;
        client->captured_key_count = reply.captured_key_count;
    }

    return SAR_COMM_OK;
}

sar_comm_status_t sar_comm_query_status(sar_comm_client_t *client,
                                        semantics_ar_status_reply_t *reply)
{
    semantics_ar_get_status_t query;
    sar_comm_status_t cs;

    if (!client || !reply)
        return SAR_COMM_ERR_PROTOCOL;

    memset(&query, 0, sizeof(query));
    memset(reply, 0, sizeof(*reply));
    sar_comm_init_header(&query.header, SEMANTICS_AR_MSG_GET_STATUS,
                         (uint32_t)sizeof(query));

    cs = sar_comm_send_recv(client, &query, (uint32_t)sizeof(query),
                            reply, (uint32_t)sizeof(*reply),
                            SEMANTICS_AR_MSG_STATUS_REPLY);
    if (cs != SAR_COMM_OK)
        return cs;
    if (reply->protocol_version != SEMANTICS_AR_PROTOCOL_VERSION)
        return SAR_COMM_ERR_VERSION;

    return SAR_COMM_OK;
}

sar_comm_status_t sar_comm_query_events(sar_comm_client_t *client,
                                        uint64_t generation, uint64_t sequence,
                                        semantics_ar_events_reply_t *reply)
{
    semantics_ar_events_query_t query;
    sar_comm_status_t cs;

    if (!client || !reply)
        return SAR_COMM_ERR_PROTOCOL;

    memset(&query, 0, sizeof(query));
    memset(reply, 0, sizeof(*reply));
    sar_comm_init_header(&query.header, SEMANTICS_AR_MSG_EVENTS_QUERY,
                         (uint32_t)sizeof(query));
    query.generation = generation;
    query.sequence = sequence;

    cs = sar_comm_send_recv(client, &query, (uint32_t)sizeof(query),
                            reply, (uint32_t)sizeof(*reply),
                            SEMANTICS_AR_MSG_EVENTS_REPLY);
    if (cs != SAR_COMM_OK)
        return cs;

    return SAR_COMM_OK;
}

sar_comm_status_t sar_comm_run(sar_comm_client_t *client,
                               const sar_comm_dispatch_t *dispatch)
{
    if (!client || !dispatch)
        return SAR_COMM_ERR_PROTOCOL;

    while (InterlockedCompareExchange(&client->running, 1, 1) == 1) {
        uint8_t  rxbuf[SAR_COMM_RECV_BUFFER];
        uint32_t rxtype = 0;
        uint32_t rxlen = 0;
        sar_comm_status_t cs;

        cs = sar_comm_recv_message(client, rxbuf, sizeof(rxbuf),
                                   &rxtype, &rxlen);
        if (cs == SAR_COMM_ERR_TRANSPORT) {
            if (InterlockedCompareExchange(&client->running, 1, 1) != 1)
                return SAR_COMM_ERR_STOPPED;
            return SAR_COMM_ERR_TRANSPORT;
        }
        if (cs != SAR_COMM_OK)
            continue;

        switch (rxtype) {
        case SEMANTICS_AR_MSG_VERDICT_NOTIFY:
            if (dispatch->on_verdict)
                dispatch->on_verdict(
                    (const semantics_ar_verdict_notify_t *)rxbuf,
                    dispatch->ctx);
            break;
        default:
            break;
        }
    }

    return SAR_COMM_ERR_STOPPED;
}

void sar_comm_stop(sar_comm_client_t *client)
{
    if (!client)
        return;
    InterlockedExchange(&client->running, 0);
    if (client->stop_event)
        SetEvent(client->stop_event);
}

void sar_comm_close(sar_comm_client_t *client)
{
    if (!client)
        return;
    if (client->port != INVALID_HANDLE_VALUE && client->port != NULL) {
        CloseHandle(client->port);
        client->port = INVALID_HANDLE_VALUE;
    }
    if (client->stop_event) {
        CloseHandle(client->stop_event);
        client->stop_event = NULL;
    }
    if (client->sign_key) {
        NCryptFreeObject((NCRYPT_KEY_HANDLE)client->sign_key);
        client->sign_key = NULL;
    }
    if (client->sign_provider) {
        NCryptFreeObject((NCRYPT_PROV_HANDLE)client->sign_provider);
        client->sign_provider = NULL;
    }
}
