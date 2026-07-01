#ifndef SEMANTICS_AR_KEYSTORE_FORMAT_H
#define SEMANTICS_AR_KEYSTORE_FORMAT_H

#include <stdint.h>

#define SEMANTICS_AR_KEYSTORE_MAGIC      0x53414B53u

#define SEMANTICS_AR_KEY_ID_SIZE         32
#define SEMANTICS_AR_MAC_SIZE            32
#define SEMANTICS_AR_MAX_KEY_BYTES       64
#define SEMANTICS_AR_IV_MAX              24
#define SEMANTICS_AR_PROVENANCE_PATH_MAX 260
#define SEMANTICS_AR_SAMPLE_TAG_MAX      64

#define SAR_ALG_AES_128   1
#define SAR_ALG_AES_192   2
#define SAR_ALG_AES_256   3
#define SAR_ALG_3DES      4
#define SAR_ALG_SM4       5
#define SAR_ALG_CAMELLIA  6
#define SAR_ALG_ARIA      7
#define SAR_ALG_SEED      8
#define SAR_ALG_CHACHA20  9
#define SAR_ALG_XCHACHA20 10
#define SAR_ALG_SALSA20   11
#define SAR_ALG_XSALSA20  12
#define SAR_ALG_RC4       13

#define SAR_MODE_ECB    1
#define SAR_MODE_CBC    2
#define SAR_MODE_CTR    3
#define SAR_MODE_XTS    4
#define SAR_MODE_CFB    5
#define SAR_MODE_OFB    6
#define SAR_MODE_STREAM 7

#pragma pack(push, 1)

typedef struct {
    uint8_t  key_id[SEMANTICS_AR_KEY_ID_SIZE];
    uint32_t algorithm;
    uint32_t mode;
    uint32_t key_length;
    uint8_t  key_bytes[SEMANTICS_AR_MAX_KEY_BYTES];
    uint8_t  iv[SEMANTICS_AR_IV_MAX];
    uint8_t  iv_length;
    uint8_t  ctr_layout_tag;
    uint64_t mode_params;
    uint64_t provenance_offset;
    uint64_t provenance_length;
    uint16_t provenance_path[SEMANTICS_AR_PROVENANCE_PATH_MAX];
    uint64_t sample_offset;
    uint32_t sample_length;
    uint8_t  sample_tag[SEMANTICS_AR_MAC_SIZE];
} semantics_ar_keystore_record_t;

typedef struct {
    uint32_t magic;
    uint64_t record_count;
    uint64_t generation;
    uint8_t  head_mac[SEMANTICS_AR_MAC_SIZE];
} semantics_ar_keystore_header_t;

typedef struct {
    semantics_ar_keystore_record_t fields;
    uint8_t                        mac[SEMANTICS_AR_MAC_SIZE];
} semantics_ar_keystore_disk_record_t;

#pragma pack(pop)

#endif
