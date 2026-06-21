#ifndef SEMANTICS_AR_SHADOW_FORMAT_H
#define SEMANTICS_AR_SHADOW_FORMAT_H

#include <stdint.h>

#define SEMANTICS_AR_SHADOW_MAGIC         0x53484457u
#define SEMANTICS_AR_INTEGRITY_TAG_SIZE   32
#define SEMANTICS_AR_SHADOW_PATH_MAX      260

#define SEMANTICS_AR_JOURNAL_OVERWRITE    1
#define SEMANTICS_AR_JOURNAL_DELETE       2
#define SEMANTICS_AR_JOURNAL_TRUNCATE     3

#define SEMANTICS_AR_SHADOW_PERCENT          5
#define SEMANTICS_AR_SHADOW_MIN_BYTES        ((int64_t)500 * 1024 * 1024)
#define SEMANTICS_AR_SHADOW_MAX_BYTES        ((int64_t)10 * 1024 * 1024 * 1024)
#define SEMANTICS_AR_SHADOW_MONOPOLY_PERCENT 80

#pragma pack(push, 1)

typedef struct {
    uint32_t magic;
    uint32_t process_id;
    uint64_t original_offset;
    uint64_t original_length;
    int64_t  timestamp;
} semantics_ar_shadow_overwrite_header_t;

typedef struct {
    uint32_t entry_type;
    uint32_t process_id;
    uint16_t original_file_path[SEMANTICS_AR_SHADOW_PATH_MAX];
    uint16_t shadow_file_path[SEMANTICS_AR_SHADOW_PATH_MAX];
    uint64_t original_offset;
    uint64_t original_length;
    int64_t  timestamp;
    uint8_t  integrity_tag[SEMANTICS_AR_INTEGRITY_TAG_SIZE];
} semantics_ar_journal_entry_t;

#pragma pack(pop)

#endif