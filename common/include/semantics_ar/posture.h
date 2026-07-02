#ifndef SEMANTICS_AR_POSTURE_H
#define SEMANTICS_AR_POSTURE_H

#include <stdint.h>

#define SAR_POSTURE_PIPE_NAME L"\\\\.\\pipe\\SemanticsAr.Posture"

#define SAR_POSTURE_FRAME_STATUS          1u
#define SAR_POSTURE_FLAG_SERVICE_RUNNING  0x1u
#define SAR_POSTURE_FLAG_DRIVER_CONNECTED 0x2u

#define SAR_POSTURE_DESCENT_NO_TPM        0x1u
#define SAR_POSTURE_DESCENT_NO_VBS_HVCI   0x2u
#define SAR_POSTURE_DESCENT_NO_PPL        0x4u

#define SAR_POSTURE_PRESERVE_UNKNOWN      0u
#define SAR_POSTURE_PRESERVE_HEALTHY      1u
#define SAR_POSTURE_PRESERVE_LOW          2u
#define SAR_POSTURE_PRESERVE_CRITICAL     3u

#define SAR_POSTURE_EXPIRY_NONE           0u
#define SAR_POSTURE_EXPIRY_GT_7D          1u
#define SAR_POSTURE_EXPIRY_LE_7D          2u
#define SAR_POSTURE_EXPIRY_LE_24H         3u

#define SAR_EVENTS_PIPE_NAME L"\\\\.\\pipe\\SemanticsAr.Events"

#define SAR_EVENTS_FRAME_EVENT 1u

#pragma pack(push, 1)

typedef struct {
    uint32_t frame_type;
    uint32_t frame_length;
    uint32_t protocol_version;
    uint32_t flags;
    uint32_t mode;
    uint64_t captured_key_count;
    uint32_t descents;
    uint32_t preserve_health;
    uint32_t oldest_expiry_bucket;
} sar_posture_frame_t;

typedef struct {
    uint32_t frame_type;
    uint32_t frame_length;
    uint32_t protocol_version;
    uint32_t valid;
    uint32_t gap;
    uint32_t event_class;
    uint64_t generation;
    uint64_t sequence;
    uint64_t timestamp;
    uint64_t actor_start_key;
} sar_events_frame_t;

#pragma pack(pop)

#endif
