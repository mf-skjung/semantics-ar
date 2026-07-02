#ifndef SEMANTICS_AR_SERVICE_COMMCLIENT_H
#define SEMANTICS_AR_SERVICE_COMMCLIENT_H

#include <windows.h>
#include <stdint.h>

#include "semantics_ar/protocol.h"

typedef enum {
    SAR_COMM_OK = 0,
    SAR_COMM_ERR_CONNECT = 1,
    SAR_COMM_ERR_KEY = 2,
    SAR_COMM_ERR_HANDSHAKE = 3,
    SAR_COMM_ERR_VERSION = 4,
    SAR_COMM_ERR_PROTOCOL = 5,
    SAR_COMM_ERR_TRANSPORT = 6,
    SAR_COMM_ERR_STOPPED = 7
} sar_comm_status_t;

typedef struct {
    HANDLE   port;
    void    *sign_key;
    void    *sign_provider;
    uint32_t mode;
    uint64_t captured_key_count;
    volatile LONG running;
} sar_comm_client_t;

typedef void (*sar_comm_verdict_cb)(const semantics_ar_verdict_notify_t *notify,
                                    void *ctx);

typedef struct {
    sar_comm_verdict_cb  on_verdict;
    void                *ctx;
} sar_comm_dispatch_t;

sar_comm_status_t sar_comm_open_signing_key(sar_comm_client_t *client,
                                            const wchar_t *key_path);

sar_comm_status_t sar_comm_connect(sar_comm_client_t *client,
                                   const wchar_t *port_name);

sar_comm_status_t sar_comm_handshake(sar_comm_client_t *client);

sar_comm_status_t sar_comm_query_status(sar_comm_client_t *client,
                                        semantics_ar_status_reply_t *reply);

sar_comm_status_t sar_comm_query_events(sar_comm_client_t *client,
                                        uint64_t generation, uint64_t sequence,
                                        semantics_ar_events_reply_t *reply);

sar_comm_status_t sar_comm_run(sar_comm_client_t *client,
                               const sar_comm_dispatch_t *dispatch);

sar_comm_status_t sar_comm_send(sar_comm_client_t *client,
                                const void *message, uint32_t length);

sar_comm_status_t sar_comm_send_recv(sar_comm_client_t *client,
                                     const void *message, uint32_t length,
                                     void *reply, uint32_t reply_capacity,
                                     uint32_t expected_type);

void sar_comm_stop(sar_comm_client_t *client);

void sar_comm_close(sar_comm_client_t *client);

#endif
