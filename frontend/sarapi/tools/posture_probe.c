/* posture_probe — Beat 0 connectivity probe for the posture-plane seam.
 *
 * Drives the existing sarapi posture-read path against a running backend and
 * reports, without any elevated privilege of its own, whether:
 *   - the session-readable posture pipe opens (FRONTEND_DESIGN XV.3),
 *   - the server-is-SYSTEM mutual-auth check passes (III.7),
 *   - the frame is protocol-valid (VII.6.1 version handling),
 *   - the redacted events pipe opens.
 * It prints its own integrity level so a medium-IL (non-elevated) run is
 * self-evident in the output. Exit code 0 iff the posture read returns OK. */

#include <windows.h>
#include <stdio.h>

#include "sarapi.h"

static const char *rstr(sarapi_result_t r)
{
    switch (r) {
    case SARAPI_OK:               return "OK";
    case SARAPI_INVALID_ARG:      return "INVALID_ARG";
    case SARAPI_PIPE_UNAVAILABLE: return "PIPE_UNAVAILABLE";
    case SARAPI_ACCESS_DENIED:    return "ACCESS_DENIED";
    case SARAPI_SERVER_UNTRUSTED: return "SERVER_UNTRUSTED";
    case SARAPI_VERSION_MISMATCH: return "VERSION_MISMATCH";
    case SARAPI_TRANSPORT_ERROR:  return "TRANSPORT_ERROR";
    default:                      return "UNKNOWN";
    }
}

static void print_integrity(void)
{
    HANDLE tok = NULL;
    DWORD  len = 0;
    TOKEN_MANDATORY_LABEL *tml;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        printf("  integrity=?(no token)\n");
        return;
    }
    GetTokenInformation(tok, TokenIntegrityLevel, NULL, 0, &len);
    tml = (TOKEN_MANDATORY_LABEL *)LocalAlloc(LPTR, len);
    if (tml && GetTokenInformation(tok, TokenIntegrityLevel, tml, len, &len)) {
        DWORD count = *GetSidSubAuthorityCount(tml->Label.Sid);
        DWORD rid   = *GetSidSubAuthority(tml->Label.Sid, count - 1);
        const char *n = rid >= SECURITY_MANDATORY_SYSTEM_RID   ? "SYSTEM"
                      : rid >= SECURITY_MANDATORY_HIGH_RID      ? "HIGH (elevated)"
                      : rid >= SECURITY_MANDATORY_MEDIUM_RID    ? "MEDIUM (standard user)"
                      :                                           "LOW";
        printf("  integrity=%s (rid=0x%lx)\n", n, (unsigned long)rid);
    } else {
        printf("  integrity=?(query failed)\n");
    }
    if (tml) LocalFree(tml);
    CloseHandle(tok);
}

int main(void)
{
    sarapi_posture_t p;
    sarapi_result_t  r;
    void            *ev = NULL;
    sarapi_result_t  er;

    printf("SARAPI_PROBE abi=%u\n", (unsigned)sarapi_abi_version());
    print_integrity();

    r = sarapi_posture_read(&p);
    printf("POSTURE result=%s\n", rstr(r));
    if (r == SARAPI_OK) {
        printf("  protocol_version=%u\n", p.protocol_version);
        printf("  service_running=%u driver_connected=%u\n",
               p.service_running, p.driver_connected);
        printf("  mode=%u captured_key_count=%llu\n",
               p.mode, (unsigned long long)p.captured_key_count);
        printf("  descents=0x%x preserve_health=%u oldest_expiry_bucket=%u\n",
               p.descents, p.preserve_health, p.oldest_expiry_bucket);
    }

    er = sarapi_events_open(&ev);
    printf("EVENTS_OPEN result=%s\n", rstr(er));
    if (er == SARAPI_OK && ev)
        sarapi_events_close(ev);

    return (r == SARAPI_OK) ? 0 : 1;
}
