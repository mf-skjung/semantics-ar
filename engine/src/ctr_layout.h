#ifndef SEMANTICS_AR_CTR_LAYOUT_H
#define SEMANTICS_AR_CTR_LAYOUT_H

#include <stdint.h>

typedef struct {
    uint8_t offset;
    uint8_t width;
    uint8_t big_endian;
    uint8_t block_size;
} sar_ctr_layout_t;

#define SAR_CTR_LAYOUT_COUNT 8

static const sar_ctr_layout_t SAR_CTR_LAYOUTS[SAR_CTR_LAYOUT_COUNT] = {
    { 12, 4, 1, 16 },
    {  8, 8, 0, 16 },
    {  8, 8, 1, 16 },
    { 14, 2, 1, 16 },
    {  0, 8, 0, 16 },
    {  4, 4, 1,  8 },
    {  0, 8, 0,  8 },
    {  0, 8, 1,  8 }
};

static inline uint64_t sar_ctr_field_mask(uint8_t width) {
    if (width >= 8) return ~(uint64_t)0;
    return (((uint64_t)1) << (8 * width)) - 1;
}

static inline uint64_t sar_ctr_field_read(const uint8_t *block, const sar_ctr_layout_t *L) {
    uint64_t v = 0;
    if (L->big_endian) {
        for (uint8_t i = 0; i < L->width; i++)
            v = (v << 8) | block[L->offset + i];
    } else {
        for (uint8_t i = 0; i < L->width; i++)
            v |= ((uint64_t)block[L->offset + i]) << (8 * i);
    }
    return v;
}

static inline void sar_ctr_field_write(uint8_t *block, const sar_ctr_layout_t *L, uint64_t v) {
    v &= sar_ctr_field_mask(L->width);
    if (L->big_endian) {
        for (uint8_t i = 0; i < L->width; i++)
            block[L->offset + L->width - 1 - i] = (uint8_t)(v >> (8 * i));
    } else {
        for (uint8_t i = 0; i < L->width; i++)
            block[L->offset + i] = (uint8_t)(v >> (8 * i));
    }
}

static inline int sar_ctr_const_equal(const uint8_t *a, const uint8_t *b,
                                      const sar_ctr_layout_t *L) {
    for (uint8_t i = 0; i < L->block_size; i++) {
        if (i >= L->offset && i < L->offset + L->width) continue;
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

#endif
