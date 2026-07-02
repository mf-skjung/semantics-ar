#ifndef SARAPI_H
#define SARAPI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SARAPI_ABI_VERSION 1u

#ifdef SARAPI_EXPORTS
#define SARAPI_API __declspec(dllexport)
#else
#define SARAPI_API __declspec(dllimport)
#endif

typedef enum {
    SARAPI_OK = 0,
    SARAPI_INVALID_ARG = 1,
    SARAPI_PIPE_UNAVAILABLE = 2,
    SARAPI_ACCESS_DENIED = 3,
    SARAPI_SERVER_UNTRUSTED = 4,
    SARAPI_VERSION_MISMATCH = 5,
    SARAPI_TRANSPORT_ERROR = 6
} sarapi_result_t;

typedef struct {
    uint32_t protocol_version;
    uint32_t service_running;
    uint32_t driver_connected;
    uint32_t mode;
    uint64_t captured_key_count;
} sarapi_posture_t;

SARAPI_API uint32_t __cdecl sarapi_abi_version(void);

SARAPI_API sarapi_result_t __cdecl sarapi_posture_read(sarapi_posture_t *out);

#ifdef __cplusplus
}
#endif

#endif
