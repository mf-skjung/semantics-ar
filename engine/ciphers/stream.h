#ifndef SEMANTICS_AR_CIPHER_STREAM_H
#define SEMANTICS_AR_CIPHER_STREAM_H

#include <stdint.h>

#define STREAM_ROUNDS_COUNT 3
extern const int STREAM_ROUNDS[STREAM_ROUNDS_COUNT];

void chacha_block(const uint8_t *key, const uint8_t *nonce,
                  uint32_t counter, int rounds, uint8_t *out);
void hchacha20(const uint8_t *key, const uint8_t *nonce, uint8_t *subkey);
void salsa_block(const uint8_t *key, const uint8_t *nonce,
                 uint64_t counter, int rounds, uint8_t *out);
void hsalsa20(const uint8_t *key, const uint8_t *nonce, uint8_t *subkey);

int rc4_verify(const uint8_t *key, uint32_t key_len,
               const uint8_t *pt, const uint8_t *ct,
               uint32_t sample_size, uint64_t file_offset);

int rc4_verify_state(const uint8_t S[256], uint8_t si, uint8_t sj,
                     const uint8_t *pt, const uint8_t *ct,
                     uint32_t sample_size);

#endif