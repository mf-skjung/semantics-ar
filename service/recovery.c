#include "recovery.h"

#include <windows.h>
#include <string.h>

#include "sar_recover_file.h"

int32_t sar_recovery_run(const sar_recovery_input_t *input,
                         uint32_t *out_files_recovered)
{
    sar_recover_file_result_t result;
    sar_recover_status_t      status;
    const sar_recovery_sample_t *confirm;

    if (out_files_recovered)
        *out_files_recovered = 0;

    if (!input || !input->target_path)
        return SAR_RECOVER_INVALID;

    confirm = input->has_confirm_sample ? &input->confirm_sample : NULL;

    memset(&result, 0, sizeof(result));
    status = sar_recover_file(input->target_path, &input->key_material,
                              &input->geometry, confirm, &result);

    if (status == SAR_RECOVER_OK && out_files_recovered)
        *out_files_recovered = 1;

    return (int32_t)status;
}

void sar_recovery_handle_request(const semantics_ar_recovery_request_t *req,
                                 sar_comm_client_t *client,
                                 const sar_recovery_dispatch_t *dispatch)
{
    semantics_ar_recovery_done_t done;
    sar_recovery_input_t         input;
    int32_t                      result = SAR_RECOVER_INVALID;
    uint32_t                     files = 0;

    if (!req || !client)
        return;

    memset(&done, 0, sizeof(done));
    done.header.protocol_version = SEMANTICS_AR_PROTOCOL_VERSION;
    done.header.message_type = SEMANTICS_AR_MSG_RECOVERY_DONE;
    done.header.message_length = (uint32_t)sizeof(done);
    memcpy(done.key_id, req->key_id, SEMANTICS_AR_KEY_ID_SIZE);

    if (dispatch && dispatch->resolve) {
        memset(&input, 0, sizeof(input));
        memcpy(input.key_id, req->key_id, SEMANTICS_AR_KEY_ID_SIZE);

        if (dispatch->resolve(req->key_id, &input, dispatch->resolve_ctx) == 0)
            result = sar_recovery_run(&input, &files);
        else
            result = SAR_RECOVER_DECLINED_KEY;

        SecureZeroMemory(&input, sizeof(input));
    }

    done.result = result;
    done.files_recovered = files;

    sar_comm_send(client, &done, (uint32_t)sizeof(done));
}
