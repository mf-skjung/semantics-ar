#include "service_internal.h"
#include <stdio.h>
#include <string.h>

void svc_apply_defaults(semantics_ar_svc_config_t *config) {
    memset(config, 0, sizeof(*config));
    config->timeout_ms = SEMANTICS_AR_CHAIN_REPLY_TIMEOUT_MS;
    config->operation_mode = SEMANTICS_AR_MODE_ENFORCE;
}

static void trim(char *s) {
    char *p = s;
    while (*p == ' ' || *p == '\t')
        p++;
    if (p != s)
        memmove(s, p, strlen(p) + 1);
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n' ||
                       s[len - 1] == ' ' || s[len - 1] == '\t')) {
        s[len - 1] = '\0';
        len--;
    }
}

BOOL svc_load_config(const char *path, semantics_ar_svc_config_t *config) {
    svc_apply_defaults(config);

    FILE *f = NULL;
    if (fopen_s(&f, path, "r") != 0 || !f)
        return FALSE;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';')
            continue;

        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        trim(key);
        trim(val);

        if (_stricmp(key, "timeout_ms") == 0) {
            config->timeout_ms = (DWORD)strtoul(val, NULL, 10);
        } else if (_stricmp(key, "operation_mode") == 0) {
            if (_stricmp(val, "audit") == 0)
                config->operation_mode = SEMANTICS_AR_MODE_AUDIT;
            else if (_stricmp(val, "enforce") == 0)
                config->operation_mode = SEMANTICS_AR_MODE_ENFORCE;
            else
                config->operation_mode = (DWORD)strtoul(val, NULL, 10);
        }
    }

    fclose(f);
    return TRUE;
}

HRESULT svc_send_config(HANDLE port, const semantics_ar_config_t *config) {
    if (port == NULL || port == INVALID_HANDLE_VALUE)
        return E_HANDLE;

    struct {
        semantics_ar_msg_header_t hdr;
        semantics_ar_config_t config;
    } msg;
    msg.hdr.message_type = SEMANTICS_AR_MSG_SET_CONFIG;
    msg.config = *config;

    DWORD bytesReturned;
    return FilterSendMessage(port, &msg, sizeof(msg), NULL, 0, &bytesReturned);
}