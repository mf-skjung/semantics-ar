#include "service_internal.h"
#include <stdio.h>

semantics_ar_svc_state_t g_svc = {};

static void report_status(DWORD state, DWORD exitCode, DWORD waitHint) {
    static DWORD checkpoint = 1;
    g_svc.Status.dwCurrentState = state;
    g_svc.Status.dwWin32ExitCode = exitCode;
    g_svc.Status.dwWaitHint = waitHint;
    if (state == SERVICE_START_PENDING)
        g_svc.Status.dwControlsAccepted = 0;
    else
        g_svc.Status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    if (state == SERVICE_RUNNING || state == SERVICE_STOPPED)
        g_svc.Status.dwCheckPoint = 0;
    else
        g_svc.Status.dwCheckPoint = checkpoint++;
    SetServiceStatus(g_svc.StatusHandle, &g_svc.Status);
}

static DWORD WINAPI ctrl_handler(DWORD control, DWORD eventType,
                                 LPVOID eventData, LPVOID context) {
    (void)eventType;
    (void)eventData;
    (void)context;
    switch (control) {
    case SERVICE_CONTROL_STOP:
        report_status(SERVICE_STOP_PENDING, NO_ERROR, 10000);
        SetEvent(g_svc.StopEvent);
        return NO_ERROR;
    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;
    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

static BOOL svc_runtime_start(void) {
    g_svc.StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_svc.StopEvent)
        return FALSE;

    InitializeSRWLock(&g_svc.PendingRestoreLock);
    memset(g_svc.PendingRestores, 0, sizeof(g_svc.PendingRestores));

    semantics_ar_svc_config_t fileConfig;
    if (!svc_load_config(SEMANTICS_AR_SVC_CONFIG_PATH, &fileConfig))
        svc_apply_defaults(&fileConfig);
    g_svc.Config = fileConfig;

    svc_volume_cache_init();
    svc_volume_cache_build();

    svc_keystore_init();
    svc_oracle_init();
    svc_whitelist_init();
    svc_chain_handler_init();

    if (svc_filter_listener_start(SEMANTICS_AR_SVC_PORT) != 0)
        return FALSE;

    g_svc.Port = svc_filter_listener_get_port();

    semantics_ar_config_t driverConfig;
    driverConfig.timeout_ms = g_svc.Config.timeout_ms;
    driverConfig.max_pending_writes = 0;
    svc_send_config(g_svc.Port, &driverConfig);

    svc_whitelist_scan_running();
    svc_reconcile_shadow_usage();

    g_svc.EvictionThread = CreateThread(NULL, 0, svc_eviction_thread, NULL, 0, NULL);
    if (!g_svc.EvictionThread)
        return FALSE;

    return TRUE;
}

static void svc_runtime_stop(void) {
    if (g_svc.StopEvent)
        SetEvent(g_svc.StopEvent);

    if (g_svc.EvictionThread) {
        WaitForSingleObject(g_svc.EvictionThread, 15000);
        CloseHandle(g_svc.EvictionThread);
        g_svc.EvictionThread = NULL;
    }

    svc_filter_listener_stop();
    g_svc.Port = NULL;

    svc_chain_handler_cleanup();
    svc_whitelist_cleanup();
    svc_oracle_cleanup();
    svc_keystore_cleanup();

    if (g_svc.StopEvent) {
        CloseHandle(g_svc.StopEvent);
        g_svc.StopEvent = NULL;
    }
}

static void WINAPI service_main(DWORD argc, LPWSTR *argv) {
    (void)argc;
    (void)argv;

    g_svc.StatusHandle = RegisterServiceCtrlHandlerExW(
        SEMANTICS_AR_SVC_NAME, ctrl_handler, NULL);
    if (!g_svc.StatusHandle)
        return;

    g_svc.Status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    report_status(SERVICE_START_PENDING, NO_ERROR, 30000);

    if (!svc_runtime_start()) {
        svc_runtime_stop();
        report_status(SERVICE_STOPPED, ERROR_GEN_FAILURE, 0);
        return;
    }

    report_status(SERVICE_RUNNING, NO_ERROR, 0);

    WaitForSingleObject(g_svc.StopEvent, INFINITE);

    svc_runtime_stop();
    report_status(SERVICE_STOPPED, NO_ERROR, 0);
}

int wmain(int argc, wchar_t **argv) {
    (void)argc;
    (void)argv;

    SERVICE_TABLE_ENTRYW table[] = {
        { (LPWSTR)SEMANTICS_AR_SVC_NAME, service_main },
        { NULL, NULL }
    };

    if (!StartServiceCtrlDispatcherW(table))
        return GetLastError();

    return 0;
}