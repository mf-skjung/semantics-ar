#ifndef SEMANTICS_AR_SAR_GATE_H
#define SEMANTICS_AR_SAR_GATE_H

#include <stddef.h>
#include <stdint.h>

#define SAR_GATE_BLOCK_SIZE 256u
#define SAR_GATE_THETA_NUM  10u
#define SAR_GATE_THETA_DEN  100u
#define SAR_GATE_MIN_NOVELTY_BIGRAMS 12u

#define SAR_GATE_MAP_BYTES  ((1u << 16) / 8u)

typedef struct {
    uint8_t present[SAR_GATE_MAP_BYTES];
} sar_gate_map_t;

typedef struct {
    int      candidate;
    uint32_t novelty_off;
    uint32_t novelty_len;
} sar_gate_result_t;

void sar_gate_block_counts(sar_gate_map_t *scratch,
                           const uint8_t *original,
                           const uint8_t *written,
                           size_t block_len,
                           uint32_t *n_query,
                           uint32_t *n_covered);

int sar_gate_fires(uint32_t n_query, uint32_t n_covered);

void sar_gate_classify(sar_gate_map_t *scratch,
                       const uint8_t *original,
                       const uint8_t *written,
                       size_t len,
                       sar_gate_result_t *out);

#endif
