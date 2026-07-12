#ifndef SEMANTICS_AR_SERVICE_PIPE_SERVER_H
#define SEMANTICS_AR_SERVICE_PIPE_SERVER_H

#include <windows.h>

#define SAR_PIPE_MAX_INSTANCES 8u

typedef struct {
    HANDLE pipe;
    HANDLE stop;
} sar_pipe_conn_t;

typedef void (*sar_pipe_serve_fn)(sar_pipe_conn_t *conn, void *ctx);

typedef struct sar_pipe_server sar_pipe_server_t;

typedef struct {
    sar_pipe_server_t *server;
    DWORD              index;
} sar_pipe_worker_t;

struct sar_pipe_server {
    HANDLE               pipes[SAR_PIPE_MAX_INSTANCES];
    HANDLE               threads[SAR_PIPE_MAX_INSTANCES];
    sar_pipe_worker_t    workers[SAR_PIPE_MAX_INSTANCES];
    DWORD                count;
    HANDLE               stop;
    PSECURITY_DESCRIPTOR sd;
    sar_pipe_serve_fn    serve;
    void                *ctx;
};

int sar_pipe_server_start(sar_pipe_server_t *server, const wchar_t *name,
                          const wchar_t *sddl, DWORD access,
                          DWORD out_buffer, DWORD in_buffer, DWORD instances,
                          sar_pipe_serve_fn serve, void *ctx);

/* Returns 1 if a worker could not be joined within the timeout (still inside an uncancelable
 * driver round-trip); in that case the worker's pipe + the shared stop event are intentionally
 * leaked rather than closed, so the returning worker cannot race a recycled handle. Returns 0 on a
 * clean stop. */
int sar_pipe_server_stop(sar_pipe_server_t *server);

BOOL sar_pipe_recv(sar_pipe_conn_t *conn, void *buffer, DWORD capacity,
                   DWORD *received, DWORD timeout_ms);

BOOL sar_pipe_send(sar_pipe_conn_t *conn, const void *buffer, DWORD length,
                   DWORD timeout_ms);

#endif
