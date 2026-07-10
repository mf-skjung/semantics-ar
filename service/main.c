#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "commclient.h"
#include "control.h"
#include "identity.h"
#include "attrib.h"

#define SAR_SERVICE_NAME L"SemanticsAr"
#define SAR_SIGNING_KEY_NAME L"SemanticsArServiceKey"

typedef struct {
    SERVICE_STATUS_HANDLE status_handle;
    SERVICE_STATUS        status;
    sar_comm_client_t     comm;
    HANDLE                stop_event;
} sar_service_t;

static sar_service_t g_service;

static void sar_set_status(DWORD state, DWORD exit_code, DWORD wait_hint)
{
    g_service.status.dwCurrentState = state;
    g_service.status.dwWin32ExitCode = exit_code;
    g_service.status.dwWaitHint = wait_hint;

    if (state == SERVICE_START_PENDING)
        g_service.status.dwControlsAccepted = 0;
    else
        g_service.status.dwControlsAccepted =
            SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;

    if (state == SERVICE_RUNNING || state == SERVICE_STOPPED)
        g_service.status.dwCheckPoint = 0;
    else
        g_service.status.dwCheckPoint++;

    if (g_service.status_handle)
        SetServiceStatus(g_service.status_handle, &g_service.status);
}

static void sar_on_verdict(const semantics_ar_verdict_notify_t *notify, void *ctx)
{
    (void)ctx;
    if (!notify)
        return;
    InterlockedIncrement64((volatile LONG64 *)&g_service.comm.captured_key_count);
}

static DWORD WINAPI sar_control_handler(DWORD control, DWORD event_type,
                                        LPVOID event_data, LPVOID context)
{
    (void)event_type;
    (void)event_data;
    (void)context;

    switch (control) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        sar_set_status(SERVICE_STOP_PENDING, NO_ERROR, 5000);
        sar_comm_stop(&g_service.comm);
        if (g_service.comm.port != INVALID_HANDLE_VALUE
            && g_service.comm.port != NULL)
            CancelIoEx(g_service.comm.port, NULL);
        if (g_service.stop_event)
            SetEvent(g_service.stop_event);
        return NO_ERROR;

    case SERVICE_CONTROL_INTERROGATE:
        SetServiceStatus(g_service.status_handle, &g_service.status);
        return NO_ERROR;

    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

static void WINAPI sar_service_main(DWORD argc, LPWSTR *argv)
{
    sar_comm_status_t cs;

    (void)argc;
    (void)argv;

    memset(&g_service.status, 0, sizeof(g_service.status));
    g_service.status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;

    g_service.status_handle =
        RegisterServiceCtrlHandlerExW(SAR_SERVICE_NAME, sar_control_handler, NULL);
    if (!g_service.status_handle)
        return;

    sar_set_status(SERVICE_START_PENDING, NO_ERROR, 5000);

    g_service.stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_service.stop_event) {
        sar_set_status(SERVICE_STOPPED, ERROR_OUTOFMEMORY, 0);
        return;
    }

    memset(&g_service.comm, 0, sizeof(g_service.comm));
    g_service.comm.port = INVALID_HANDLE_VALUE;

    sar_set_status(SERVICE_RUNNING, NO_ERROR, 0);

    cs = sar_comm_open_signing_key(&g_service.comm, SAR_SIGNING_KEY_NAME);
    if (cs != SAR_COMM_OK) {
        sar_set_status(SERVICE_STOPPED, ERROR_NO_SUCH_PRIVILEGE, 0);
        return;
    }

    cs = sar_comm_connect(&g_service.comm, SAR_COMM_PORT_NAME);
    if (cs != SAR_COMM_OK) {
        sar_comm_close(&g_service.comm);
        sar_set_status(SERVICE_STOPPED, ERROR_CONNECTION_REFUSED, 0);
        return;
    }

    cs = sar_comm_handshake(&g_service.comm);
    if (cs != SAR_COMM_OK) {
        sar_comm_close(&g_service.comm);
        sar_set_status(SERVICE_STOPPED,
                       cs == SAR_COMM_ERR_VERSION
                           ? ERROR_REVISION_MISMATCH
                           : ERROR_AUTHENTICATION_FIREWALL_FAILED,
                       0);
        return;
    }

    sar_attrib_init();

    if (sar_control_listener_start(&g_service.comm) != 0) {
        sar_comm_close(&g_service.comm);
        sar_set_status(SERVICE_STOPPED, ERROR_GEN_FAILURE, 0);
        return;
    }

    {
        sar_comm_dispatch_t dispatch;
        dispatch.on_verdict = sar_on_verdict;
        dispatch.ctx = NULL;
        sar_comm_run(&g_service.comm, &dispatch);
    }

    sar_control_listener_stop();

    sar_comm_close(&g_service.comm);

    if (g_service.stop_event) {
        CloseHandle(g_service.stop_event);
        g_service.stop_event = NULL;
    }

    sar_set_status(SERVICE_STOPPED, NO_ERROR, 0);
}

int wmain(int argc, wchar_t **argv)
{
    SERVICE_TABLE_ENTRYW table[] = {
        { (LPWSTR)SAR_SERVICE_NAME, sar_service_main },
        { NULL, NULL }
    };

    (void)argc;
    (void)argv;

    if (!StartServiceCtrlDispatcherW(table))
        return (int)GetLastError();

    return 0;
}
