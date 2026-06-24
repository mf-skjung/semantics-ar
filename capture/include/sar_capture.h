#ifndef SEMANTICS_AR_SAR_CAPTURE_H
#define SEMANTICS_AR_SAR_CAPTURE_H

#include <stddef.h>
#include <stdint.h>
#include "conviction_engine.h"
#include "sar_gate.h"
#include "sar_keystore.h"
#include "semantics_ar/protocol.h"

typedef enum {
    SAR_CAPTURE_INVALID = 0,
    SAR_CAPTURE_SKIP_GATE = 1,
    SAR_CAPTURE_NO_CONVICTION = 2,
    SAR_CAPTURE_CONVICTED = 3
} sar_capture_outcome_t;

typedef struct {
    const uint8_t  *plaintext;
    const uint8_t  *ciphertext;
    uint32_t        sample_size;
    uint64_t        file_offset;
    const uint8_t (*candidates)[SAR_CANDIDATE_SIZE];
    size_t          candidate_count;
    const uint8_t  *scan_buffer;
    size_t          scan_length;
    const uint16_t *provenance_path;
    uint64_t        provenance_offset;
} sar_capture_request_t;

typedef struct {
    sar_capture_outcome_t          outcome;
    sar_gate_result_t              gate;
    sar_verdict_t                  verdict;
    semantics_ar_keystore_record_t record;
    semantics_ar_verdict_notify_t  notify;
} sar_capture_result_t;

void sar_capture_result_zero(sar_capture_result_t *result);

sar_capture_outcome_t sar_capture_run(const sar_capture_request_t *req,
                                      sar_gate_map_t *gate_scratch,
                                      const uint8_t mac_key[SEMANTICS_AR_MAC_SIZE],
                                      sar_capture_result_t *out);

void sar_capture_build_notify(const semantics_ar_keystore_record_t *record,
                              semantics_ar_verdict_notify_t *notify);

#endif
