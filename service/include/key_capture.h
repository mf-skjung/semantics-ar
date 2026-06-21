#ifndef SEMANTICS_AR_KEY_CAPTURE_H
#define SEMANTICS_AR_KEY_CAPTURE_H

#include <stdint.h>
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ORACLE_ALG_AES_128   1
#define ORACLE_ALG_AES_192   2
#define ORACLE_ALG_AES_256   3
#define ORACLE_ALG_3DES      4
#define ORACLE_ALG_SM4       5
#define ORACLE_ALG_CAMELLIA  6
#define ORACLE_ALG_ARIA      7
#define ORACLE_ALG_SEED      8
#define ORACLE_ALG_CHACHA20  9
#define ORACLE_ALG_XCHACHA20 10
#define ORACLE_ALG_SALSA20   11
#define ORACLE_ALG_XSALSA20  12
#define ORACLE_ALG_RC4       13
#define ORACLE_ALG_SOSEMANUK 14
#define ORACLE_ALG_HC128     15

#define ORACLE_MODE_ECB    1
#define ORACLE_MODE_CBC    2
#define ORACLE_MODE_CTR    3
#define ORACLE_MODE_XTS    4
#define ORACLE_MODE_CFB    5
#define ORACLE_MODE_OFB    6
#define ORACLE_MODE_STREAM 7

#define ORACLE_MAX_CANDIDATES 256

typedef struct {
    uint8_t  value[16];
    uint32_t thread_id;
    uint32_t register_index;
} oracle_candidate_t;

typedef struct {
    int      confirmed;
    uint8_t  key[128];
    uint32_t key_length;
    uint32_t algorithm;
    uint32_t mode;
    uint32_t thread_id;
    uint32_t register_index;
} oracle_result_t;

typedef int (*block_cipher_fn)(void *ctx, const uint8_t *in, uint8_t *out);

typedef struct {
    block_cipher_fn encrypt;
    block_cipher_fn decrypt;
    void           *ctx;
    uint32_t        block_size;
} block_cipher_ctx_t;

void svc_oracle_init(void);
void svc_oracle_cleanup(void);

int svc_oracle_test(
    DWORD target_pid,
    DWORD thread_id,
    const uint8_t *plaintext,
    const uint8_t *ciphertext,
    uint32_t sample_size,
    uint64_t file_offset,
    oracle_result_t *result);

#ifdef __cplusplus
}
#endif

#endif