#include "service_internal.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    FILTER_MESSAGE_HEADER header;
    semantics_ar_chain_notification_t chain;
} filter_message_t;

typedef struct {
    FILTER_REPLY_HEADER header;
    semantics_ar_chain_response_t response;
} filter_reply_t;

static HANDLE g_port = INVALID_HANDLE_VALUE;
static volatile LONG g_running = 0;
static HANDLE g_listener_thread = NULL;

static DWORD WINAPI listener_thread_proc(LPVOID param) {
    (void)param;

    DWORD msg_size = sizeof(filter_message_t);
    filter_message_t *msg = (filter_message_t *)calloc(1, msg_size);
    if (!msg)
        return 1;

    while (InterlockedCompareExchange(&g_running, 0, 0) != 0) {
        memset(msg, 0, msg_size);
        HRESULT hr = FilterGetMessage(g_port, &msg->header, msg_size, NULL);
        if (FAILED(hr)) {
            if (hr == HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED))
                break;
            Sleep(1);
            continue;
        }

        if (msg->chain.message_type != SEMANTICS_AR_MSG_CHAIN_CANDIDATE)
            continue;

        filter_reply_t reply;
        memset(&reply, 0, sizeof(reply));
        reply.header.Status = 0;
        reply.header.MessageId = msg->header.MessageId;

        DWORD pid = msg->chain.process_id;
        uint32_t action = svc_handle_chain_candidate(&msg->chain, &reply.response);

        FilterReplyMessage(g_port, &reply.header, sizeof(reply));

        if (action == SEMANTICS_AR_ACTION_BLOCK)
            svc_post_oracle_confirmation(g_port, pid);
    }

    free(msg);
    return 0;
}

int svc_filter_listener_start(const wchar_t *port_name) {
    HRESULT hr = FilterConnectCommunicationPort(
        port_name, 0, NULL, 0, NULL, &g_port);
    if (FAILED(hr))
        return -1;

    InterlockedExchange(&g_running, 1);

    g_listener_thread = CreateThread(NULL, 0, listener_thread_proc, NULL, 0, NULL);
    if (!g_listener_thread) {
        CloseHandle(g_port);
        g_port = INVALID_HANDLE_VALUE;
        InterlockedExchange(&g_running, 0);
        return -1;
    }

    return 0;
}

void svc_filter_listener_stop(void) {
    InterlockedExchange(&g_running, 0);

    if (g_port != INVALID_HANDLE_VALUE)
        CancelIoEx(g_port, NULL);

    if (g_listener_thread) {
        WaitForSingleObject(g_listener_thread, 5000);
        CloseHandle(g_listener_thread);
        g_listener_thread = NULL;
    }

    if (g_port != INVALID_HANDLE_VALUE) {
        CloseHandle(g_port);
        g_port = INVALID_HANDLE_VALUE;
    }
}

HANDLE svc_filter_listener_get_port(void) {
    return g_port;
}