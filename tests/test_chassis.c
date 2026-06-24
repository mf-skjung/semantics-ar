#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "sar_control.h"

static int g_pass;
static int g_fail;

#define CHECK(cond, name) do {                                  \
    if (cond) { g_pass++; printf("  ok   %s\n", (name)); }       \
    else { g_fail++; printf("  FAIL %s\n", (name)); }            \
} while (0)

static uint32_t lcg_state;

static void lcg_seed(uint32_t s) { lcg_state = s ? s : 0x1234567u; }

static uint8_t lcg_byte(void) {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return (uint8_t)(lcg_state >> 24);
}

static void set_header(semantics_ar_msg_header_t *h, uint32_t version,
                       uint32_t type, uint32_t length) {
    h->protocol_version = version;
    h->message_type = type;
    h->message_length = length;
}

static void test_msg_validation(void) {
    uint32_t type = 0xffffffffu;
    semantics_ar_set_mode_t sm;
    uint8_t buf[sizeof(sm)];

    memset(&sm, 0, sizeof(sm));
    set_header(&sm.header, SEMANTICS_AR_PROTOCOL_VERSION,
               SEMANTICS_AR_MSG_SET_MODE, (uint32_t)sizeof(sm));
    sm.mode = SEMANTICS_AR_MODE_ENFORCE;
    memcpy(buf, &sm, sizeof(sm));

    CHECK(sar_msg_validate(buf, sizeof(buf), &type) == SAR_MSG_OK,
          "valid SET_MODE accepted");
    CHECK(type == SEMANTICS_AR_MSG_SET_MODE, "valid SET_MODE out_type set");

    CHECK(sar_msg_validate(buf, sizeof(semantics_ar_msg_header_t) - 1u, &type)
              == SAR_MSG_ERR_SHORT_HEADER, "short header rejected");
    CHECK(type == 0u, "short header clears out_type");

    CHECK(sar_msg_validate(NULL, 64u, &type) == SAR_MSG_ERR_SHORT_HEADER,
          "null buffer rejected");

    set_header(&sm.header, SEMANTICS_AR_PROTOCOL_VERSION + 1u,
               SEMANTICS_AR_MSG_SET_MODE, (uint32_t)sizeof(sm));
    memcpy(buf, &sm, sizeof(sm));
    CHECK(sar_msg_validate(buf, sizeof(buf), &type) == SAR_MSG_ERR_VERSION,
          "wrong version rejected before body");

    set_header(&sm.header, SEMANTICS_AR_PROTOCOL_VERSION, 999u,
               (uint32_t)sizeof(sm));
    memcpy(buf, &sm, sizeof(sm));
    CHECK(sar_msg_validate(buf, sizeof(buf), &type) == SAR_MSG_ERR_TYPE,
          "unknown type rejected");

    set_header(&sm.header, SEMANTICS_AR_PROTOCOL_VERSION,
               SEMANTICS_AR_MSG_SET_MODE, (uint32_t)sizeof(sm) + 1u);
    memcpy(buf, &sm, sizeof(sm));
    CHECK(sar_msg_validate(buf, sizeof(buf), &type) == SAR_MSG_ERR_LENGTH,
          "declared length mismatch rejected");

    set_header(&sm.header, SEMANTICS_AR_PROTOCOL_VERSION,
               SEMANTICS_AR_MSG_SET_MODE, (uint32_t)sizeof(sm));
    memcpy(buf, &sm, sizeof(sm));
    CHECK(sar_msg_validate(buf, sizeof(buf) - 1u, &type) == SAR_MSG_ERR_TRUNCATED,
          "inbound shorter than declared rejected");
    CHECK(sar_msg_validate(buf, sizeof(buf) + 1u, &type) == SAR_MSG_ERR_OVERSIZED,
          "inbound longer than declared rejected");

    CHECK(sar_msg_expected_length(SEMANTICS_AR_MSG_VERDICT_NOTIFY)
              == sizeof(semantics_ar_verdict_notify_t), "expected len verdict");
    CHECK(sar_msg_expected_length(SEMANTICS_AR_MSG_RECOVERY_REQUEST)
              == sizeof(semantics_ar_recovery_request_t), "expected len rec req");
    CHECK(sar_msg_expected_length(SEMANTICS_AR_MSG_RECOVERY_DONE)
              == sizeof(semantics_ar_recovery_done_t), "expected len rec done");
    CHECK(sar_msg_expected_length(SEMANTICS_AR_MSG_WHITELIST_ADD)
              == sizeof(semantics_ar_whitelist_control_t), "expected len wl add");
    CHECK(sar_msg_expected_length(SEMANTICS_AR_MSG_GET_STATUS)
              == sizeof(semantics_ar_get_status_t), "expected len get status");
    CHECK(sar_msg_expected_length(SEMANTICS_AR_MSG_CONNECT_CHALLENGE)
              == sizeof(semantics_ar_connect_challenge_t), "expected len challenge");
    CHECK(sar_msg_expected_length(SEMANTICS_AR_MSG_CONNECT_RESPONSE)
              == sizeof(semantics_ar_connect_response_t), "expected len response");
    CHECK(sar_msg_expected_length(SEMANTICS_AR_MSG_STATUS_REPLY)
              == sizeof(semantics_ar_status_reply_t), "expected len status reply");
    CHECK(sar_msg_expected_length(0u) == 0u, "expected len unknown is zero");
}

static void test_msg_fuzz(void) {
    uint8_t buf[600];
    int bad = 0;
    lcg_seed(0xC0FFEEu);
    for (int iter = 0; iter < 200000; iter++) {
        size_t len = (size_t)(lcg_byte() | ((uint32_t)lcg_byte() << 8)) % (sizeof(buf) + 1u);
        for (size_t i = 0; i < len; i++)
            buf[i] = lcg_byte();
        uint32_t type = 0xffffffffu;
        sar_msg_status_t r = sar_msg_validate(buf, len, &type);
        if (r == SAR_MSG_OK) {
            if (type < 1u || type > 10u) bad = 1;
            if (len != (size_t)sar_msg_expected_length(type)) bad = 1;
        } else {
            if (type != 0u) bad = 1;
        }
    }
    CHECK(bad == 0, "fuzz: no spurious accept, out_type invariant holds");
}

static void make_identity(sar_identity_t *id, uint16_t p, uint16_t s, uint8_t h) {
    memset(id, 0, sizeof(*id));
    id->image_path[0] = p;
    id->cert_subject[0] = s;
    id->content_hash[0] = h;
}

static void test_whitelist(void) {
    sar_identity_t storage[4];
    sar_whitelist_t wl;
    sar_identity_t a, b, c, d, e, near;

    sar_whitelist_init(&wl, storage, 4u);
    make_identity(&a, 1, 1, 1);
    make_identity(&b, 2, 2, 2);
    make_identity(&c, 3, 3, 3);
    make_identity(&d, 4, 4, 4);
    make_identity(&e, 5, 5, 5);

    CHECK(sar_whitelist_match(&wl, &a) == 0, "empty whitelist no match");
    CHECK(sar_whitelist_add(&wl, &a) == SAR_WL_OK, "add a");
    CHECK(sar_whitelist_match(&wl, &a) == 1, "a matches after add");
    CHECK(sar_whitelist_add(&wl, &a) == SAR_WL_DUPLICATE, "duplicate add rejected");

    make_identity(&near, 1, 1, 9);
    CHECK(sar_whitelist_match(&wl, &near) == 0, "near miss (hash) no match");
    make_identity(&near, 1, 9, 1);
    CHECK(sar_whitelist_match(&wl, &near) == 0, "near miss (subject) no match");
    make_identity(&near, 9, 1, 1);
    CHECK(sar_whitelist_match(&wl, &near) == 0, "near miss (path) no match");

    CHECK(sar_whitelist_add(&wl, &b) == SAR_WL_OK, "add b");
    CHECK(sar_whitelist_add(&wl, &c) == SAR_WL_OK, "add c");
    CHECK(sar_whitelist_add(&wl, &d) == SAR_WL_OK, "add d");
    CHECK(sar_whitelist_add(&wl, &e) == SAR_WL_FULL, "capacity enforced");

    CHECK(sar_whitelist_remove(&wl, &b) == SAR_WL_OK, "remove b");
    CHECK(sar_whitelist_match(&wl, &b) == 0, "b gone after remove");
    CHECK(sar_whitelist_match(&wl, &a) == 1, "a survives compaction");
    CHECK(sar_whitelist_match(&wl, &c) == 1, "c survives compaction");
    CHECK(sar_whitelist_match(&wl, &d) == 1, "d survives compaction");
    CHECK(sar_whitelist_remove(&wl, &b) == SAR_WL_NOT_FOUND, "remove absent");
    CHECK(sar_whitelist_add(&wl, &e) == SAR_WL_OK, "room freed after remove");
}

static void test_identity_resolve(void) {
    CHECK(sar_identity_resolve(0, 0) == SAR_IDSTATE_OBSERVE_PENDING,
          "unverified -> observe-pending");
    CHECK(sar_identity_resolve(0, 1) == SAR_IDSTATE_OBSERVE_PENDING,
          "unverified never exempt even on whitelist hit");
    CHECK(sar_identity_resolve(1, 0) == SAR_IDSTATE_OBSERVE,
          "verified non-listed -> observe");
    CHECK(sar_identity_resolve(1, 1) == SAR_IDSTATE_EXEMPT,
          "verified + listed -> exempt");
}

static void test_mode(void) {
    sar_mode_state_t st;
    sar_mode_init(&st);
    CHECK(sar_mode_get(&st) == SEMANTICS_AR_MODE_AUDIT, "default mode AUDIT");
    CHECK(sar_mode_set(&st, SEMANTICS_AR_MODE_ENFORCE) == 1, "set ENFORCE ok");
    CHECK(sar_mode_get(&st) == SEMANTICS_AR_MODE_ENFORCE, "mode is ENFORCE");
    CHECK(sar_mode_set(&st, 7u) == 0, "invalid mode rejected");
    CHECK(sar_mode_get(&st) == SEMANTICS_AR_MODE_ENFORCE, "invalid set preserves mode");
    CHECK(sar_mode_set(&st, SEMANTICS_AR_MODE_AUDIT) == 1, "set AUDIT ok");
    CHECK(sar_mode_get(&st) == SEMANTICS_AR_MODE_AUDIT, "mode is AUDIT");
}

static int verify_good(const uint8_t *nonce, uint32_t nonce_len,
                       const uint8_t *sig, uint32_t sig_len, void *ctx) {
    (void)ctx;
    if (sig_len != nonce_len)
        return 0;
    for (uint32_t i = 0; i < nonce_len; i++) {
        if (sig[i] != (uint8_t)(nonce[i] ^ 0x5Au))
            return 0;
    }
    return 1;
}

static void sign_nonce(const uint8_t *nonce, uint8_t *sig, uint32_t n) {
    for (uint32_t i = 0; i < n; i++)
        sig[i] = (uint8_t)(nonce[i] ^ 0x5Au);
}

static void test_handshake(void) {
    sar_handshake_t hs;
    uint8_t nonce[SAR_HS_NONCE_SIZE];
    uint8_t sig[SAR_HS_NONCE_SIZE];

    lcg_seed(0xABCDu);
    for (uint32_t i = 0; i < SAR_HS_NONCE_SIZE; i++)
        nonce[i] = lcg_byte();

    sar_handshake_init(&hs, SEMANTICS_AR_PROTOCOL_VERSION, verify_good, NULL);
    CHECK(sar_handshake_verify_response(&hs, sig, SAR_HS_NONCE_SIZE)
              == SAR_HS_RESULT_SEQUENCE, "verify before challenge -> sequence");
    CHECK(sar_handshake_issue_challenge(&hs, nonce, 8u) == SAR_HS_RESULT_BAD_PARAM,
          "wrong nonce length rejected");
    CHECK(sar_handshake_issue_challenge(&hs, nonce, SAR_HS_NONCE_SIZE)
              == SAR_HS_RESULT_OK, "challenge issued");
    CHECK(sar_handshake_issue_challenge(&hs, nonce, SAR_HS_NONCE_SIZE)
              == SAR_HS_RESULT_SEQUENCE, "double challenge -> sequence");
    sign_nonce(hs.nonce, sig, SAR_HS_NONCE_SIZE);
    CHECK(sar_handshake_verify_response(&hs, sig, SAR_HS_NONCE_SIZE)
              == SAR_HS_RESULT_OK, "good signature authenticates");
    CHECK(sar_handshake_check_version(&hs, SEMANTICS_AR_PROTOCOL_VERSION)
              == SAR_HS_RESULT_OK, "matching version ok");
    CHECK(sar_handshake_authenticated(&hs) == 1, "fully authenticated");
    sar_handshake_timeout(&hs);
    CHECK(sar_handshake_authenticated(&hs) == 1, "timeout after complete is no-op");

    sar_handshake_init(&hs, SEMANTICS_AR_PROTOCOL_VERSION, verify_good, NULL);
    sar_handshake_issue_challenge(&hs, nonce, SAR_HS_NONCE_SIZE);
    memset(sig, 0, sizeof(sig));
    CHECK(sar_handshake_verify_response(&hs, sig, SAR_HS_NONCE_SIZE)
              == SAR_HS_RESULT_VERIFY_FAILED, "bad signature fails");
    CHECK(sar_handshake_authenticated(&hs) == 0, "rejected not authenticated");
    CHECK(sar_handshake_check_version(&hs, SEMANTICS_AR_PROTOCOL_VERSION)
              == SAR_HS_RESULT_SEQUENCE, "no version step after reject");

    sar_handshake_init(&hs, SEMANTICS_AR_PROTOCOL_VERSION, verify_good, NULL);
    sar_handshake_issue_challenge(&hs, nonce, SAR_HS_NONCE_SIZE);
    sign_nonce(hs.nonce, sig, SAR_HS_NONCE_SIZE);
    sar_handshake_verify_response(&hs, sig, SAR_HS_NONCE_SIZE);
    CHECK(sar_handshake_check_version(&hs, SEMANTICS_AR_PROTOCOL_VERSION + 1u)
              == SAR_HS_RESULT_VERSION_MISMATCH, "version mismatch rejected");
    CHECK(sar_handshake_authenticated(&hs) == 0, "version mismatch not authenticated");

    sar_handshake_init(&hs, SEMANTICS_AR_PROTOCOL_VERSION, verify_good, NULL);
    sar_handshake_issue_challenge(&hs, nonce, SAR_HS_NONCE_SIZE);
    sar_handshake_timeout(&hs);
    CHECK(sar_handshake_authenticated(&hs) == 0, "timeout before complete rejects");
    sign_nonce(hs.nonce, sig, SAR_HS_NONCE_SIZE);
    CHECK(sar_handshake_verify_response(&hs, sig, SAR_HS_NONCE_SIZE)
              == SAR_HS_RESULT_SEQUENCE, "no verify after timeout reject");
}

int main(void) {
    printf("test_chassis\n");
    test_msg_validation();
    test_msg_fuzz();
    test_whitelist();
    test_identity_resolve();
    test_mode();
    test_handshake();
    printf("chassis: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
