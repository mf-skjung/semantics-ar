#ifndef SEMANTICS_AR_BATTERY_SEAM_H
#define SEMANTICS_AR_BATTERY_SEAM_H

#include <stdint.h>

#define SEMANTICS_AR_DESTRUCT_OVERWRITE      1
#define SEMANTICS_AR_DESTRUCT_TRUNCATE       2
#define SEMANTICS_AR_DESTRUCT_DELETE         3
#define SEMANTICS_AR_DESTRUCT_SECTION_WRITE  4
#define SEMANTICS_AR_DESTRUCT_RENAME_OVER    5
#define SEMANTICS_AR_DESTRUCT_HARDLINK_OVER  6
#define SEMANTICS_AR_DESTRUCT_BLOCK_CLONE    7
#define SEMANTICS_AR_DESTRUCT_FSCTL          8

#define SEMANTICS_AR_BATTERY_SAMPLE_MAX 256

#pragma pack(push, 1)

typedef struct _semantics_ar_battery_request {
    uint8_t  destruction_member;
    uint64_t file_offset;
    uint32_t novelty_slice_offset;
    uint32_t novelty_slice_length;
    uint32_t sample_size;
    uint8_t  plaintext[SEMANTICS_AR_BATTERY_SAMPLE_MAX];
    uint8_t  ciphertext[SEMANTICS_AR_BATTERY_SAMPLE_MAX];
} semantics_ar_battery_request_t;

#pragma pack(pop)

#endif
