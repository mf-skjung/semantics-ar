#ifndef SEMANTICS_AR_SERVICE_RECOVERY_H
#define SEMANTICS_AR_SERVICE_RECOVERY_H

#include <stdint.h>

#include "semantics_ar/protocol.h"
#include "sar_recover.h"
#include "commclient.h"

typedef struct {
    uint8_t              key_id[SEMANTICS_AR_KEY_ID_SIZE];
    sar_recovery_key_t   key_material;
    sar_geometry_t       geometry;
    sar_recovery_sample_t confirm_sample;
    int                  has_confirm_sample;
    const char          *target_path;
} sar_recovery_input_t;

typedef int (*sar_recovery_resolver_fn)(const uint8_t *key_id,
                                        sar_recovery_input_t *out,
                                        void *ctx);

typedef struct {
    sar_recovery_resolver_fn resolve;
    void                    *resolve_ctx;
} sar_recovery_dispatch_t;

int32_t sar_recovery_run(const sar_recovery_input_t *input,
                         uint32_t *out_files_recovered);

void sar_recovery_handle_request(const semantics_ar_recovery_request_t *req,
                                 sar_comm_client_t *client,
                                 const sar_recovery_dispatch_t *dispatch);

#endif
