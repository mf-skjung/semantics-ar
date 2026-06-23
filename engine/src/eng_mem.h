#ifndef SEMANTICS_AR_ENG_MEM_H
#define SEMANTICS_AR_ENG_MEM_H

#include <stddef.h>
#include <stdint.h>

static inline void sar_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++)
        d[i] = s[i];
}

static inline void sar_memset(void *dst, uint8_t v, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++)
        d[i] = v;
}

static inline int sar_memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i])
            return x[i] < y[i] ? -1 : 1;
    }
    return 0;
}

static inline int sar_ct_equal(const void *a, const void *b, size_t n) {
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    uint8_t acc = 0;
    for (size_t i = 0; i < n; i++)
        acc = (uint8_t)(acc | (x[i] ^ y[i]));
    return acc == 0;
}

static inline void sar_secure_zero(void *dst, size_t n) {
    volatile uint8_t *d = (volatile uint8_t *)dst;
    for (size_t i = 0; i < n; i++)
        d[i] = 0;
}

#endif
