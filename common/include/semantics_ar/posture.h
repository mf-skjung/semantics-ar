#ifndef SEMANTICS_AR_POSTURE_H
#define SEMANTICS_AR_POSTURE_H

#include <stdint.h>

#define SAR_POSTURE_PIPE_NAME L"\\\\.\\pipe\\SemanticsAr.Posture"

#define SAR_POSTURE_FRAME_STATUS          1u
#define SAR_POSTURE_FLAG_SERVICE_RUNNING  0x1u
#define SAR_POSTURE_FLAG_DRIVER_CONNECTED 0x2u

#pragma pack(push, 1)

typedef struct {
    uint32_t frame_type;
    uint32_t frame_length;
    uint32_t protocol_version;
    uint32_t flags;
    uint32_t mode;
    uint64_t captured_key_count;
} sar_posture_frame_t;

#pragma pack(pop)

#endif
