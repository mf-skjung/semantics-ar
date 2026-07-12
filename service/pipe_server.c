#include "pipe_server.h"

#include <sddl.h>
#include <string.h>

static HANDLE sar_pipe_create(const wchar_t *name, SECURITY_ATTRIBUTES *sa,
                              DWORD access, DWORD out_buffer, DWORD in_buffer,
                              DWORD instances, BOOL first)
{
    DWORD open_mode = access | FILE_FLAG_OVERLAPPED;

    if (first)
        open_mode |= FILE_FLAG_FIRST_PIPE_INSTANCE;

    return CreateNamedPipeW(name, open_mode,
                            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE
                            | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                            instances, out_buffer, in_buffer, 0, sa);
}

BOOL sar_pipe_recv(sar_pipe_conn_t *conn, void *buffer, DWORD capacity,
                   DWORD *received, DWORD timeout_ms)
{
    OVERLAPPED ov;
    HANDLE     ev;
    DWORD      n = 0;
    BOOL       ok;

    ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ev)
        return FALSE;

    memset(&ov, 0, sizeof(ov));
    ov.hEvent = ev;

    ok = ReadFile(conn->pipe, buffer, capacity, &n, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        HANDLE waits[2];
        DWORD  w;

        waits[0] = ev;
        waits[1] = conn->stop;
        w = WaitForMultipleObjects(2, waits, FALSE, timeout_ms);
        if (w == WAIT_OBJECT_0) {
            ok = GetOverlappedResult(conn->pipe, &ov, &n, FALSE);
        } else {
            CancelIoEx(conn->pipe, &ov);
            GetOverlappedResult(conn->pipe, &ov, &n, TRUE);
            ok = FALSE;
        }
    }

    CloseHandle(ev);
    if (received)
        *received = n;
    return ok;
}

BOOL sar_pipe_send(sar_pipe_conn_t *conn, const void *buffer, DWORD length,
                   DWORD timeout_ms)
{
    OVERLAPPED ov;
    HANDLE     ev;
    DWORD      n = 0;
    BOOL       ok;

    ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ev)
        return FALSE;

    memset(&ov, 0, sizeof(ov));
    ov.hEvent = ev;

    ok = WriteFile(conn->pipe, buffer, length, &n, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        HANDLE waits[2];
        DWORD  w;

        waits[0] = ev;
        waits[1] = conn->stop;
        w = WaitForMultipleObjects(2, waits, FALSE, timeout_ms);
        if (w == WAIT_OBJECT_0) {
            ok = GetOverlappedResult(conn->pipe, &ov, &n, FALSE);
        } else {
            CancelIoEx(conn->pipe, &ov);
            GetOverlappedResult(conn->pipe, &ov, &n, TRUE);
            ok = FALSE;
        }
    }

    CloseHandle(ev);
    return ok && n == length;
}

static DWORD WINAPI sar_pipe_worker_proc(LPVOID param)
{
    sar_pipe_worker_t *worker = (sar_pipe_worker_t *)param;
    sar_pipe_server_t *server = worker->server;
    HANDLE             pipe = server->pipes[worker->index];
    OVERLAPPED         ov;
    HANDLE             ev;

    ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ev)
        return 0;

    for (;;) {
        BOOL connected = FALSE;

        if (WaitForSingleObject(server->stop, 0) == WAIT_OBJECT_0)
            break;

        memset(&ov, 0, sizeof(ov));
        ov.hEvent = ev;
        ResetEvent(ev);

        if (ConnectNamedPipe(pipe, &ov)) {
            connected = TRUE;
        } else {
            DWORD e = GetLastError();

            if (e == ERROR_PIPE_CONNECTED) {
                connected = TRUE;
            } else if (e == ERROR_IO_PENDING) {
                HANDLE waits[2];
                DWORD  w;
                DWORD  n = 0;

                waits[0] = ev;
                waits[1] = server->stop;
                w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (w == WAIT_OBJECT_0) {
                    connected = GetOverlappedResult(pipe, &ov, &n, FALSE);
                } else {
                    CancelIoEx(pipe, &ov);
                    GetOverlappedResult(pipe, &ov, &n, TRUE);
                    break;
                }
            }
        }

        if (connected) {
            sar_pipe_conn_t conn;

            conn.pipe = pipe;
            conn.stop = server->stop;
            server->serve(&conn, server->ctx);
            FlushFileBuffers(pipe);
        }
        DisconnectNamedPipe(pipe);
    }

    CloseHandle(ev);
    return 0;
}

int sar_pipe_server_start(sar_pipe_server_t *server, const wchar_t *name,
                          const wchar_t *sddl, DWORD access,
                          DWORD out_buffer, DWORD in_buffer, DWORD instances,
                          sar_pipe_serve_fn serve, void *ctx)
{
    SECURITY_ATTRIBUTES sa;
    DWORD               i;

    if (!server || !name || !sddl || !serve
        || instances == 0 || instances > SAR_PIPE_MAX_INSTANCES)
        return -1;

    memset(server, 0, sizeof(*server));
    server->serve = serve;
    server->ctx = ctx;
    server->count = instances;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl, SDDL_REVISION_1, &server->sd, NULL))
        return -1;

    server->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!server->stop) {
        LocalFree(server->sd);
        server->sd = NULL;
        return -1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = server->sd;
    sa.bInheritHandle = FALSE;

    for (i = 0; i < instances; i++) {
        server->pipes[i] = sar_pipe_create(name, &sa, access, out_buffer,
                                           in_buffer, instances, i == 0);
        if (server->pipes[i] == INVALID_HANDLE_VALUE) {
            DWORD j;

            for (j = 0; j < i; j++)
                CloseHandle(server->pipes[j]);
            CloseHandle(server->stop);
            server->stop = NULL;
            LocalFree(server->sd);
            server->sd = NULL;
            return -1;
        }
    }

    for (i = 0; i < instances; i++) {
        server->workers[i].server = server;
        server->workers[i].index = i;
        server->threads[i] = CreateThread(NULL, 0, sar_pipe_worker_proc,
                                           &server->workers[i], 0, NULL);
        if (!server->threads[i]) {
            DWORD j;

            SetEvent(server->stop);
            for (j = 0; j < instances; j++)
                CancelIoEx(server->pipes[j], NULL);
            for (j = 0; j < i; j++) {
                WaitForSingleObject(server->threads[j], 5000);
                CloseHandle(server->threads[j]);
            }
            for (j = 0; j < instances; j++)
                CloseHandle(server->pipes[j]);
            CloseHandle(server->stop);
            server->stop = NULL;
            LocalFree(server->sd);
            server->sd = NULL;
            return -1;
        }
    }

    return 0;
}

int sar_pipe_server_stop(sar_pipe_server_t *server)
{
    DWORD i;
    BOOL  stuck = FALSE;

    if (!server || !server->stop)
        return 0;

    SetEvent(server->stop);
    for (i = 0; i < server->count; i++) {
        if (server->pipes[i] && server->pipes[i] != INVALID_HANDLE_VALUE)
            CancelIoEx(server->pipes[i], NULL);
    }
    for (i = 0; i < server->count; i++) {
        if (server->threads[i]) {
            if (WaitForSingleObject(server->threads[i], 5000) == WAIT_OBJECT_0) {
                CloseHandle(server->threads[i]);
                server->threads[i] = NULL;
            } else {
                /* Worker still inside an uncancelable FilterSendMessage (e.g. a recover whose target
                 * filesystem has stalled). CancelIoEx cannot rescue an IOCTL parked in the driver's
                 * message callback - it returns only when the callback returns. */
                stuck = TRUE;
            }
        }
    }
    /* Close only what no live worker can still reference. A stuck worker's conn still holds its pipe
     * and the (already-signaled) stop event; closing them would hand the worker a recycled handle
     * when it finally returns and calls sar_pipe_send. Each worker owns exactly one pipe (by index),
     * so an exited worker's pipe is safe to close; a stuck worker's pipe and the shared stop event
     * are leaked. The stop event is signaled, so a stuck worker exits on its own once its round-trip
     * returns, and the process is terminating, so the OS reclaims the leak at exit. */
    for (i = 0; i < server->count; i++) {
        if (server->threads[i] == NULL &&
            server->pipes[i] && server->pipes[i] != INVALID_HANDLE_VALUE) {
            CloseHandle(server->pipes[i]);
            server->pipes[i] = INVALID_HANDLE_VALUE;
        }
    }
    if (!stuck) {
        CloseHandle(server->stop);
        server->stop = NULL;
        if (server->sd) {
            LocalFree(server->sd);
            server->sd = NULL;
        }
    }
    return stuck ? 1 : 0;
}
