#include <windows.h>
#include <stddef.h>

#include "sarapi.h"
#include "server_identity.h"
#include "semantics_ar/posture.h"
#include "semantics_ar/protocol.h"

_Static_assert(sizeof(sarapi_event_t) == 48, "sarapi_event_t layout");

#define SARAPI_EVENTS_BUSY_WAIT_MS     2000u
#define SARAPI_EVENTS_CONNECT_ATTEMPTS 3u

static sarapi_result_t sarapi_open_events(HANDLE *out_pipe)
{
    DWORD attempt;

    for (attempt = 0; attempt < SARAPI_EVENTS_CONNECT_ATTEMPTS; attempt++) {
        HANDLE pipe = CreateFileW(SAR_EVENTS_PIPE_NAME,
                                  GENERIC_READ | FILE_WRITE_ATTRIBUTES,
                                  0, NULL, OPEN_EXISTING,
                                  SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION,
                                  NULL);
        if (pipe != INVALID_HANDLE_VALUE) {
            *out_pipe = pipe;
            return SARAPI_OK;
        }

        switch (GetLastError()) {
        case ERROR_PIPE_BUSY:
            if (!WaitNamedPipeW(SAR_EVENTS_PIPE_NAME, SARAPI_EVENTS_BUSY_WAIT_MS))
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

sarapi_result_t __cdecl sarapi_events_open(void **out_handle)
{
    HANDLE          pipe = INVALID_HANDLE_VALUE;
    DWORD           mode = PIPE_READMODE_MESSAGE;
    sarapi_result_t r;

    if (!out_handle)
        return SARAPI_INVALID_ARG;

    *out_handle = NULL;

    r = sarapi_open_events(&pipe);
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

    *out_handle = (void *)pipe;
    return SARAPI_OK;
}

sarapi_result_t __cdecl sarapi_events_read(void *handle, sarapi_event_t *out)
{
    sar_events_frame_t frame;
    DWORD               got = 0;
    BOOL                ok;

    if (!handle || handle == INVALID_HANDLE_VALUE || !out)
        return SARAPI_INVALID_ARG;

    memset(out, 0, sizeof(*out));
    memset(&frame, 0, sizeof(frame));

    ok = ReadFile((HANDLE)handle, &frame, (DWORD)sizeof(frame), &got, NULL);
    if (!ok) {
        DWORD e = GetLastError();

        return (e == ERROR_MORE_DATA) ? SARAPI_VERSION_MISMATCH
                                      : SARAPI_TRANSPORT_ERROR;
    }

    if (got < offsetof(sar_events_frame_t, valid))
        return SARAPI_TRANSPORT_ERROR;
    if (frame.frame_type != SAR_EVENTS_FRAME_EVENT)
        return SARAPI_TRANSPORT_ERROR;
    if (frame.protocol_version != SEMANTICS_AR_PROTOCOL_VERSION)
        return SARAPI_VERSION_MISMATCH;
    if (got != (DWORD)sizeof(frame)
        || frame.frame_length != (uint32_t)sizeof(frame))
        return SARAPI_VERSION_MISMATCH;

    out->valid = frame.valid;
    out->gap = frame.gap;
    out->event_class = frame.event_class;
    out->generation = frame.generation;
    out->sequence = frame.sequence;
    out->timestamp = frame.timestamp;
    out->actor_start_key = frame.actor_start_key;
    return SARAPI_OK;
}

void __cdecl sarapi_events_close(void *handle)
{
    if (handle && handle != INVALID_HANDLE_VALUE)
        CloseHandle((HANDLE)handle);
}
