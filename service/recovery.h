#ifndef SEMANTICS_AR_SERVICE_RECOVERY_H
#define SEMANTICS_AR_SERVICE_RECOVERY_H

#include <stdint.h>

#include "commclient.h"

int32_t sar_recovery_run(sar_comm_client_t *client,
                         const uint8_t *key_id,
                         const wchar_t *target_path,
                         uint64_t *out_bytes);

int32_t sar_preserve_recovery_run(sar_comm_client_t *client,
                                  const wchar_t *target_path,
                                  uint64_t offset,
                                  uint64_t length,
                                  uint64_t *out_bytes);

#endif
