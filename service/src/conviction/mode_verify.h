#ifndef SEMANTICS_AR_MODE_VERIFY_H
#define SEMANTICS_AR_MODE_VERIFY_H

#include <stdint.h>
#include <windows.h>
#include <bcrypt.h>
#include "key_capture.h"

void gf128_mul_alpha(uint8_t *tweak);
int  is_candidate_viable(const uint8_t *data);
int  is_rc4_sbox(const uint8_t *data);

int try_bcrypt_modes(BCRYPT_KEY_HANDLE hk,
                     const uint8_t *pt, const uint8_t *ct, uint32_t len,
                     uint32_t *out_mode);

int verify_bcrypt_xts(BCRYPT_KEY_HANDLE hk1, BCRYPT_KEY_HANDLE hk2,
                      const uint8_t *pt, const uint8_t *ct,
                      uint32_t len, uint64_t file_offset);

int generic_try_all_modes(const block_cipher_ctx_t *bc,
                          const uint8_t *pt, const uint8_t *ct,
                          uint32_t len, uint32_t *out_mode);

int generic_verify_xts(const block_cipher_ctx_t *bc1, const block_cipher_ctx_t *bc2,
                       const uint8_t *pt, const uint8_t *ct,
                       uint32_t len, uint64_t file_offset);

#endif