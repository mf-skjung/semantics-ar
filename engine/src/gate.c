#include "sar_gate.h"
#include "eng_mem.h"

static void map_build_overlapping(sar_gate_map_t *m,
                                  const uint8_t *original,
                                  size_t block_len)
{
    sar_memset(m->present, 0, SAR_GATE_MAP_BYTES);
    if (block_len < 2)
        return;
    for (size_t i = 0; i + 1 < block_len; i++) {
        uint32_t pair = ((uint32_t)original[i] << 8) | (uint32_t)original[i + 1];
        m->present[pair >> 3] |= (uint8_t)(1u << (pair & 7u));
    }
}

void sar_gate_block_counts(sar_gate_map_t *scratch,
                           const uint8_t *original,
                           const uint8_t *written,
                           size_t block_len,
                           uint32_t *n_query,
                           uint32_t *n_covered)
{
    map_build_overlapping(scratch, original, block_len);

    uint32_t nq = 0;
    uint32_t nc = 0;
    for (size_t k = 0; k + 1 < block_len; k++) {
        if (written[k] == original[k] && written[k + 1] == original[k + 1])
            continue;
        uint32_t pair = ((uint32_t)written[k] << 8) | (uint32_t)written[k + 1];
        nq++;
        if ((scratch->present[pair >> 3] >> (pair & 7u)) & 1u)
            nc++;
    }

    *n_query = nq;
    *n_covered = nc;
}

int sar_gate_fires(uint32_t n_query, uint32_t n_covered)
{
    if (n_query == 0)
        return 0;
    uint32_t novel = n_query - n_covered;
    if (novel < SAR_GATE_MIN_NOVELTY_BIGRAMS)
        return 0;
    return novel * SAR_GATE_THETA_DEN >= n_query * SAR_GATE_THETA_NUM;
}

void sar_gate_classify(sar_gate_map_t *scratch,
                       const uint8_t *original,
                       const uint8_t *written,
                       size_t len,
                       sar_gate_result_t *out)
{
    out->candidate = 0;
    out->novelty_off = 0;
    out->novelty_len = 0;

    int seen = 0;
    size_t first = 0;
    size_t last = 0;

    for (size_t off = 0; off < len; off += SAR_GATE_BLOCK_SIZE) {
        size_t block_len = len - off;
        if (block_len > SAR_GATE_BLOCK_SIZE)
            block_len = SAR_GATE_BLOCK_SIZE;

        uint32_t nq;
        uint32_t nc;
        sar_gate_block_counts(scratch, original + off, written + off,
                              block_len, &nq, &nc);

        if (sar_gate_fires(nq, nc)) {
            if (!seen) {
                first = off;
                seen = 1;
            }
            last = off + block_len;
        }
    }

    if (seen) {
        out->candidate = 1;
        out->novelty_off = (uint32_t)first;
        out->novelty_len = (uint32_t)(last - first);
    }
}
