#ifndef SEMANTICS_AR_SAR_RECOVER_FILE_H
#define SEMANTICS_AR_SAR_RECOVER_FILE_H

#include <stdint.h>
#include "sar_recover.h"

typedef struct {
    sar_recover_status_t status;
    uint64_t             encrypted_bytes;
    uint64_t             file_size;
} sar_recover_file_result_t;

sar_recover_status_t sar_recover_file(const char *path,
                                      const sar_recovery_key_t *rk,
                                      const sar_geometry_t *geom,
                                      const sar_recovery_verify_t *verify,
                                      sar_recover_file_result_t *out);

int sar_atomic_replace_file(const char *target_path, const char *replacement_path);

#endif
