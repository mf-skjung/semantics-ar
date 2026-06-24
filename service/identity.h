#ifndef SEMANTICS_AR_SERVICE_IDENTITY_H
#define SEMANTICS_AR_SERVICE_IDENTITY_H

#include <windows.h>
#include <stdint.h>

#include "semantics_ar/protocol.h"
#include "sar_control.h"

typedef enum {
    SAR_IDENTITY_VERDICT_VERIFIED = 0,
    SAR_IDENTITY_VERDICT_UNSIGNED = 1,
    SAR_IDENTITY_VERDICT_HASH_FAILED = 2,
    SAR_IDENTITY_VERDICT_PATH_FAILED = 3,
    SAR_IDENTITY_VERDICT_ERROR = 4
} sar_identity_verdict_t;

typedef struct {
    sar_identity_t        identity;
    sar_identity_verdict_t verdict;
} sar_identity_eval_t;

sar_identity_verdict_t sar_identity_evaluate(const wchar_t *image_path,
                                             sar_identity_eval_t *out);

#endif
