#ifndef SEMANTICS_AR_PRESERVE_FORMAT_H
#define SEMANTICS_AR_PRESERVE_FORMAT_H

#include <stdint.h>
#include "semantics_ar/keystore_format.h"

#define SEMANTICS_AR_PRESERVE_MAGIC 0x50524153u

#define SAR_PRESERVE_PROBATION 0u
#define SAR_PRESERVE_PROTECTED 1u

#define SAR_PRESERVE_KIND_BYTERANGE 0u
#define SAR_PRESERVE_KIND_NAMESPACE 1u

#pragma pack(push, 1)

typedef struct {
    uint16_t provenance_path[SEMANTICS_AR_PROVENANCE_PATH_MAX];
    uint64_t provenance_offset;
    uint64_t provenance_length;
    uint64_t capture_time;
    uint64_t payload_offset;
    uint64_t payload_length;
    uint64_t actor_id;
    uint32_t state;
    uint8_t  kind;
    uint8_t  iv[SEMANTICS_AR_IV_MAX];
    uint8_t  iv_length;
    uint8_t  content_tag[SEMANTICS_AR_MAC_SIZE];
} sar_preserve_record_t;

typedef struct {
    uint32_t magic;
    uint64_t record_count;
    uint64_t generation;
    uint8_t  head_mac[SEMANTICS_AR_MAC_SIZE];
} sar_preserve_header_t;

typedef struct {
    sar_preserve_record_t fields;
    uint8_t               mac[SEMANTICS_AR_MAC_SIZE];
} sar_preserve_disk_record_t;

#pragma pack(pop)

#endif
