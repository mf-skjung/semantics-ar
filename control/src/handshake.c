#include "sar_control.h"
#include "ctl_mem.h"

void sar_handshake_init(sar_handshake_t *hs, uint32_t version_local,
                        sar_hs_verify_fn verify, void *verify_ctx) {
    hs->phase = SAR_HS_INIT;
    ctl_memset(hs->nonce, 0, SAR_HS_NONCE_SIZE);
    hs->nonce_len = 0u;
    hs->version_local = version_local;
    hs->version_peer = 0u;
    hs->verify = verify;
    hs->verify_ctx = verify_ctx;
}

sar_hs_result_t sar_handshake_issue_challenge(sar_handshake_t *hs,
                                              const uint8_t *random,
                                              uint32_t random_len) {
    if (hs == NULL || random == NULL)
        return SAR_HS_RESULT_BAD_PARAM;
    if (random_len != SAR_HS_NONCE_SIZE)
        return SAR_HS_RESULT_BAD_PARAM;
    if (hs->phase != SAR_HS_INIT)
        return SAR_HS_RESULT_SEQUENCE;
    ctl_memcpy(hs->nonce, random, SAR_HS_NONCE_SIZE);
    hs->nonce_len = SAR_HS_NONCE_SIZE;
    hs->phase = SAR_HS_CHALLENGE_ISSUED;
    return SAR_HS_RESULT_OK;
}

sar_hs_result_t sar_handshake_verify_response(sar_handshake_t *hs,
                                              const uint8_t *signature,
                                              uint32_t sig_len) {
    if (hs == NULL || signature == NULL)
        return SAR_HS_RESULT_BAD_PARAM;
    if (hs->phase != SAR_HS_CHALLENGE_ISSUED)
        return SAR_HS_RESULT_SEQUENCE;
    if (hs->verify == NULL) {
        hs->phase = SAR_HS_REJECTED;
        return SAR_HS_RESULT_BAD_PARAM;
    }
    if (!hs->verify(hs->nonce, hs->nonce_len, signature, sig_len, hs->verify_ctx)) {
        hs->phase = SAR_HS_REJECTED;
        return SAR_HS_RESULT_VERIFY_FAILED;
    }
    hs->phase = SAR_HS_AUTHENTICATED;
    return SAR_HS_RESULT_OK;
}

sar_hs_result_t sar_handshake_check_version(sar_handshake_t *hs,
                                            uint32_t peer_version) {
    if (hs == NULL)
        return SAR_HS_RESULT_BAD_PARAM;
    if (hs->phase != SAR_HS_AUTHENTICATED)
        return SAR_HS_RESULT_SEQUENCE;
    hs->version_peer = peer_version;
    if (peer_version != hs->version_local) {
        hs->phase = SAR_HS_REJECTED;
        return SAR_HS_RESULT_VERSION_MISMATCH;
    }
    hs->phase = SAR_HS_VERSIONED;
    return SAR_HS_RESULT_OK;
}

void sar_handshake_timeout(sar_handshake_t *hs) {
    if (hs == NULL)
        return;
    if (hs->phase != SAR_HS_VERSIONED)
        hs->phase = SAR_HS_REJECTED;
}

int sar_handshake_authenticated(const sar_handshake_t *hs) {
    return hs != NULL && hs->phase == SAR_HS_VERSIONED;
}
