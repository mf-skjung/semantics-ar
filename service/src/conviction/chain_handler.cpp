#include "service_internal.h"
#include <string.h>

static volatile LONG g_chain_ready;

void svc_chain_handler_init(void) {
    InterlockedExchange(&g_chain_ready, 1);
}

void svc_chain_handler_cleanup(void) {
    InterlockedExchange(&g_chain_ready, 0);
}

uint32_t svc_handle_chain_candidate(const semantics_ar_chain_notification_t *notif,
                                    semantics_ar_chain_response_t *response) {
    response->action = SEMANTICS_AR_ACTION_ALLOW;

    if (!InterlockedCompareExchange(&g_chain_ready, 1, 1))
        return SEMANTICS_AR_ACTION_ALLOW;

    if (notif->sample_size < 16)
        return SEMANTICS_AR_ACTION_ALLOW;

    uint32_t sample = notif->sample_size;
    if (sample > SEMANTICS_AR_ORACLE_SAMPLE_SIZE)
        sample = SEMANTICS_AR_ORACLE_SAMPLE_SIZE;

    oracle_result_t result;
    int confirmed = svc_oracle_test(
        notif->process_id, notif->thread_id,
        notif->plaintext_sample, notif->ciphertext_sample,
        sample, notif->file_offset, &result);

    if (!confirmed)
        return SEMANTICS_AR_ACTION_ALLOW;

    svc_keystore_store(&result);
    response->action = SEMANTICS_AR_ACTION_BLOCK;
    return SEMANTICS_AR_ACTION_BLOCK;
}

void svc_post_oracle_confirmation(HANDLE port, DWORD pid) {
    if (port == NULL || port == INVALID_HANDLE_VALUE || pid == 0)
        return;

    struct {
        semantics_ar_msg_header_t header;
        semantics_ar_confirm_payload_t payload;
    } msg;

    msg.header.message_type = SEMANTICS_AR_MSG_CONFIRMED;
    msg.payload.target_pid = pid;

    DWORD bytesReturned = 0;
    FilterSendMessage(port, &msg, sizeof(msg), NULL, 0, &bytesReturned);

    DWORD tree[SEMANTICS_AR_MAX_ACTIVE_PROCESSES];
    DWORD treeCount = svc_collect_process_tree(pid, tree, SEMANTICS_AR_MAX_ACTIVE_PROCESSES);

    svc_add_pending_restore(pid, tree, treeCount);

    for (DWORD i = 0; i < treeCount; i++)
        svc_terminate_process(tree[i]);

    svc_execute_restore(pid);
}