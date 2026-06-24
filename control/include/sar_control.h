#ifndef SEMANTICS_AR_SAR_CONTROL_H
#define SEMANTICS_AR_SAR_CONTROL_H

#include <stddef.h>
#include <stdint.h>
#include "semantics_ar/protocol.h"

typedef enum {
    SAR_MSG_OK = 0,
    SAR_MSG_ERR_SHORT_HEADER = 1,
    SAR_MSG_ERR_VERSION = 2,
    SAR_MSG_ERR_TYPE = 3,
    SAR_MSG_ERR_LENGTH = 4,
    SAR_MSG_ERR_TRUNCATED = 5,
    SAR_MSG_ERR_OVERSIZED = 6
} sar_msg_status_t;

uint32_t sar_msg_expected_length(uint32_t message_type);

sar_msg_status_t sar_msg_validate(const uint8_t *buf, size_t inbound_len,
                                  uint32_t *out_type);

typedef struct {
    uint16_t image_path[SEMANTICS_AR_PROTO_PATH_MAX];
    uint16_t cert_subject[SEMANTICS_AR_PROTO_SUBJECT_MAX];
    uint8_t  content_hash[SEMANTICS_AR_CONTENT_HASH_SIZE];
} sar_identity_t;

typedef struct {
    sar_identity_t *entries;
    uint32_t count;
    uint32_t capacity;
} sar_whitelist_t;

typedef enum {
    SAR_WL_OK = 0,
    SAR_WL_FULL = 1,
    SAR_WL_DUPLICATE = 2,
    SAR_WL_NOT_FOUND = 3,
    SAR_WL_INVALID = 4
} sar_wl_status_t;

void sar_whitelist_init(sar_whitelist_t *wl, sar_identity_t *storage,
                        uint32_t capacity);
int sar_whitelist_match(const sar_whitelist_t *wl, const sar_identity_t *id);
sar_wl_status_t sar_whitelist_add(sar_whitelist_t *wl, const sar_identity_t *id);
sar_wl_status_t sar_whitelist_remove(sar_whitelist_t *wl, const sar_identity_t *id);

typedef enum {
    SAR_IDSTATE_OBSERVE_PENDING = 0,
    SAR_IDSTATE_OBSERVE = 1,
    SAR_IDSTATE_EXEMPT = 2
} sar_id_state_t;

sar_id_state_t sar_identity_resolve(int verified, int whitelist_hit);

typedef struct {
    uint32_t mode;
} sar_mode_state_t;

void sar_mode_init(sar_mode_state_t *st);
int sar_mode_set(sar_mode_state_t *st, uint32_t requested);
uint32_t sar_mode_get(const sar_mode_state_t *st);

#define SAR_HS_NONCE_SIZE 32u

typedef enum {
    SAR_HS_INIT = 0,
    SAR_HS_CHALLENGE_ISSUED = 1,
    SAR_HS_AUTHENTICATED = 2,
    SAR_HS_VERSIONED = 3,
    SAR_HS_REJECTED = 4
} sar_hs_phase_t;

typedef enum {
    SAR_HS_RESULT_OK = 0,
    SAR_HS_RESULT_SEQUENCE = 1,
    SAR_HS_RESULT_VERIFY_FAILED = 2,
    SAR_HS_RESULT_VERSION_MISMATCH = 3,
    SAR_HS_RESULT_BAD_PARAM = 4
} sar_hs_result_t;

typedef int (*sar_hs_verify_fn)(const uint8_t *nonce, uint32_t nonce_len,
                                const uint8_t *signature, uint32_t sig_len,
                                void *ctx);

typedef struct {
    sar_hs_phase_t phase;
    uint8_t  nonce[SAR_HS_NONCE_SIZE];
    uint32_t nonce_len;
    uint32_t version_local;
    uint32_t version_peer;
    sar_hs_verify_fn verify;
    void *verify_ctx;
} sar_handshake_t;

void sar_handshake_init(sar_handshake_t *hs, uint32_t version_local,
                        sar_hs_verify_fn verify, void *verify_ctx);
sar_hs_result_t sar_handshake_issue_challenge(sar_handshake_t *hs,
                                              const uint8_t *random,
                                              uint32_t random_len);
sar_hs_result_t sar_handshake_verify_response(sar_handshake_t *hs,
                                              const uint8_t *signature,
                                              uint32_t sig_len);
sar_hs_result_t sar_handshake_check_version(sar_handshake_t *hs,
                                            uint32_t peer_version);
void sar_handshake_timeout(sar_handshake_t *hs);
int sar_handshake_authenticated(const sar_handshake_t *hs);

#endif
