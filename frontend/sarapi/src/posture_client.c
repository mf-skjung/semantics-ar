#include <windows.h>
#include <stddef.h>

#include "sarapi.h"
#include "server_identity.h"
#include "semantics_ar/posture.h"
#include "semantics_ar/protocol.h"

_Static_assert(sizeof(sarapi_posture_t) == 48, "sarapi_posture_t layout");
_Static_assert(sizeof(sar_posture_frame_t) == 40, "sar_posture_frame_t layout");

#define SARAPI_BUSY_WAIT_MS     2000u
#define SARAPI_CONNECT_ATTEMPTS 3u

uint32_t __cdecl sarapi_abi_version(void)
{
    return SARAPI_ABI_VERSION;
}

static sarapi_result_t sarapi_open_posture(HANDLE *out_pipe)
{
    DWORD attempt;

    for (attempt = 0; attempt < SARAPI_CONNECT_ATTEMPTS; attempt++) {
        HANDLE pipe = CreateFileW(SAR_POSTURE_PIPE_NAME,
                                  GENERIC_READ | FILE_WRITE_ATTRIBUTES,
                                  0, NULL, OPEN_EXISTING,
                                  FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT
                                      | SECURITY_IDENTIFICATION,
                                  NULL);
        if (pipe != INVALID_HANDLE_VALUE) {
            *out_pipe = pipe;
            return SARAPI_OK;
        }

        switch (GetLastError()) {
        case ERROR_PIPE_BUSY:
            if (!WaitNamedPipeW(SAR_POSTURE_PIPE_NAME, SARAPI_BUSY_WAIT_MS))
                return SARAPI_PIPE_UNAVAILABLE;
            break;
        case ERROR_ACCESS_DENIED:
            return SARAPI_ACCESS_DENIED;
        default:
            return SARAPI_PIPE_UNAVAILABLE;
        }
    }
    return SARAPI_PIPE_UNAVAILABLE;
}

sarapi_result_t __cdecl sarapi_posture_read(sarapi_posture_t *out)
{
    HANDLE              pipe = INVALID_HANDLE_VALUE;
    sar_posture_frame_t frame;
    DWORD               mode = PIPE_READMODE_MESSAGE;
    DWORD               got = 0;
    sarapi_result_t     r;

    if (!out)
        return SARAPI_INVALID_ARG;

    memset(out, 0, sizeof(*out));

    r = sarapi_open_posture(&pipe);
    if (r != SARAPI_OK)
        return r;

    r = sarapi_server_is_system(pipe);
    if (r != SARAPI_OK) {
        CloseHandle(pipe);
        return r;
    }

    if (!SetNamedPipeHandleState(pipe, &mode, NULL, NULL)) {
        CloseHandle(pipe);
        return SARAPI_TRANSPORT_ERROR;
    }

    memset(&frame, 0, sizeof(frame));
    r = sarapi_read_frame(pipe, &frame, (DWORD)sizeof(frame), &got);
    CloseHandle(pipe);
    if (r != SARAPI_OK)
        return r;

    if (got < offsetof(sar_posture_frame_t, flags))
        return SARAPI_TRANSPORT_ERROR;
    if (frame.frame_type != SAR_POSTURE_FRAME_STATUS)
        return SARAPI_TRANSPORT_ERROR;
    if (frame.protocol_version != SEMANTICS_AR_PROTOCOL_VERSION)
        return SARAPI_VERSION_MISMATCH;
    if (got != (DWORD)sizeof(frame)
        || frame.frame_length != (uint32_t)sizeof(frame))
        return SARAPI_VERSION_MISMATCH;

    out->protocol_version = frame.protocol_version;
    out->service_running =
        (frame.flags & SAR_POSTURE_FLAG_SERVICE_RUNNING) ? 1u : 0u;
    out->driver_connected =
        (frame.flags & SAR_POSTURE_FLAG_DRIVER_CONNECTED) ? 1u : 0u;
    out->integrity_halt =
        (frame.flags & SAR_POSTURE_FLAG_INTEGRITY_HALT) ? 1u : 0u;
    out->mode = frame.mode;
    out->captured_key_count = frame.captured_key_count;
    out->descents = frame.descents;
    out->preserve_health = frame.preserve_health;
    out->oldest_expiry_bucket = frame.oldest_expiry_bucket;
    return SARAPI_OK;
}
