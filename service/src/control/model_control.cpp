#include "service_internal.h"

HRESULT svc_push_driver_config(HANDLE port, const semantics_ar_svc_config_t *config) {
    if (port == NULL || port == INVALID_HANDLE_VALUE)
        return E_HANDLE;
    if (!config)
        return E_INVALIDARG;

    semantics_ar_config_t driverConfig;
    driverConfig.timeout_ms = config->timeout_ms;
    driverConfig.max_pending_writes = 0;

    return svc_send_config(port, &driverConfig);
}