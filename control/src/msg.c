#include "sar_control.h"
#include "ctl_mem.h"

uint32_t sar_msg_expected_length(uint32_t message_type) {
    switch (message_type) {
    case SEMANTICS_AR_MSG_VERDICT_NOTIFY:
        return (uint32_t)sizeof(semantics_ar_verdict_notify_t);
    case SEMANTICS_AR_MSG_RECOVERY_EXEC:
        return (uint32_t)sizeof(semantics_ar_recovery_exec_t);
    case SEMANTICS_AR_MSG_RECOVERY_RESULT:
        return (uint32_t)sizeof(semantics_ar_recovery_result_t);
    case SEMANTICS_AR_MSG_SET_MODE:
        return (uint32_t)sizeof(semantics_ar_set_mode_t);
    case SEMANTICS_AR_MSG_WHITELIST_ADD:
        return (uint32_t)sizeof(semantics_ar_whitelist_control_t);
    case SEMANTICS_AR_MSG_WHITELIST_REMOVE:
        return (uint32_t)sizeof(semantics_ar_whitelist_control_t);
    case SEMANTICS_AR_MSG_GET_STATUS:
        return (uint32_t)sizeof(semantics_ar_get_status_t);
    case SEMANTICS_AR_MSG_CONNECT_CHALLENGE:
        return (uint32_t)sizeof(semantics_ar_connect_challenge_t);
    case SEMANTICS_AR_MSG_CONNECT_RESPONSE:
        return (uint32_t)sizeof(semantics_ar_connect_response_t);
    case SEMANTICS_AR_MSG_STATUS_REPLY:
        return (uint32_t)sizeof(semantics_ar_status_reply_t);
    case SEMANTICS_AR_MSG_CATALOG_QUERY:
        return (uint32_t)sizeof(semantics_ar_catalog_query_t);
    case SEMANTICS_AR_MSG_CATALOG_REPLY:
        return (uint32_t)sizeof(semantics_ar_catalog_reply_t);
    case SEMANTICS_AR_MSG_PRESERVE_QUERY:
        return (uint32_t)sizeof(semantics_ar_preserve_query_t);
    case SEMANTICS_AR_MSG_PRESERVE_REPLY:
        return (uint32_t)sizeof(semantics_ar_preserve_reply_t);
    case SEMANTICS_AR_MSG_PRESERVE_RECOVER:
        return (uint32_t)sizeof(semantics_ar_preserve_recover_t);
    case SEMANTICS_AR_MSG_PRESERVE_RESULT:
        return (uint32_t)sizeof(semantics_ar_recovery_result_t);
    case SEMANTICS_AR_MSG_SET_BUDGET:
        return (uint32_t)sizeof(semantics_ar_set_budget_t);
    default:
        return 0u;
    }
}

sar_msg_status_t sar_msg_validate(const uint8_t *buf, size_t inbound_len,
                                  uint32_t *out_type) {
    semantics_ar_msg_header_t header;
    uint32_t expected;

    if (out_type != NULL)
        *out_type = 0u;
    if (buf == NULL)
        return SAR_MSG_ERR_SHORT_HEADER;
    if (inbound_len < sizeof(header))
        return SAR_MSG_ERR_SHORT_HEADER;

    ctl_memcpy(&header, buf, sizeof(header));

    if (header.protocol_version != SEMANTICS_AR_PROTOCOL_VERSION)
        return SAR_MSG_ERR_VERSION;

    expected = sar_msg_expected_length(header.message_type);
    if (expected == 0u)
        return SAR_MSG_ERR_TYPE;
    if (header.message_length != expected)
        return SAR_MSG_ERR_LENGTH;
    if (inbound_len < (size_t)expected)
        return SAR_MSG_ERR_TRUNCATED;
    if (inbound_len > (size_t)expected)
        return SAR_MSG_ERR_OVERSIZED;

    if (out_type != NULL)
        *out_type = header.message_type;
    return SAR_MSG_OK;
}
