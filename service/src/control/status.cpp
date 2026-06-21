#include "service_internal.h"

semantics_ar_result_t svc_query_driver_status(BOOL *installed, BOOL *running) {
    if (installed)
        *installed = FALSE;
    if (running)
        *running = FALSE;

    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCM)
        return SEMANTICS_AR_OK;

    SC_HANDLE hSvc = OpenServiceW(hSCM, SEMANTICS_AR_SVC_NAME, SERVICE_QUERY_STATUS);
    if (!hSvc) {
        CloseServiceHandle(hSCM);
        return SEMANTICS_AR_OK;
    }

    if (installed)
        *installed = TRUE;

    SERVICE_STATUS ss;
    if (QueryServiceStatus(hSvc, &ss) && ss.dwCurrentState == SERVICE_RUNNING) {
        if (running)
            *running = TRUE;
    }

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return SEMANTICS_AR_OK;
}

semantics_ar_result_t svc_query_protection_status(HANDLE port,
                                                  semantics_ar_status_reply_t *reply) {
    if (!reply)
        return SEMANTICS_AR_ERROR_INVALID_PARAM;
    if (port == NULL || port == INVALID_HANDLE_VALUE)
        return SEMANTICS_AR_ERROR_DRIVER_NOT_LOADED;

    semantics_ar_msg_header_t header;
    header.message_type = SEMANTICS_AR_MSG_GET_STATUS;

    DWORD bytesReturned = 0;
    HRESULT hr = FilterSendMessage(port, &header, sizeof(header),
                                   reply, sizeof(*reply), &bytesReturned);
    if (FAILED(hr) || bytesReturned < sizeof(*reply))
        return SEMANTICS_AR_ERROR_INTERNAL;

    return SEMANTICS_AR_OK;
}