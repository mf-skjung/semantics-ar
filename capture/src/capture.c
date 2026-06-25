#include "sar_capture.h"
#include "eng_mem.h"

void sar_capture_result_zero(sar_capture_result_t *result)
{
    if (!result)
        return;
    sar_memset(result, 0, sizeof(*result));
    result->outcome = SAR_CAPTURE_INVALID;
}

void sar_capture_build_notify(const semantics_ar_keystore_record_t *record,
                              semantics_ar_verdict_notify_t *notify)
{
    sar_memset(notify, 0, sizeof(*notify));
    notify->header.protocol_version = SEMANTICS_AR_PROTOCOL_VERSION;
    notify->header.message_type = SEMANTICS_AR_MSG_VERDICT_NOTIFY;
    notify->header.message_length = (uint32_t)sizeof(*notify);
    sar_memcpy(notify->key_id, record->key_id, SEMANTICS_AR_KEY_ID_SIZE);
    notify->algorithm = record->algorithm;
    notify->mode = record->mode;
    notify->mode_params = record->mode_params;
    notify->provenance_offset = record->provenance_offset;
    for (uint32_t i = 0; i < (uint32_t)SEMANTICS_AR_PROTO_PATH_MAX; i++)
        notify->provenance_path[i] = record->provenance_path[i];
}

sar_capture_outcome_t sar_capture_run(const sar_capture_request_t *req,
                                      sar_gate_map_t *gate_scratch,
                                      const uint8_t mac_key[SEMANTICS_AR_MAC_SIZE],
                                      sar_capture_result_t *out)
{
    sar_engine_input_t in;

    sar_capture_result_zero(out);
    if (!req || !out || !gate_scratch || !mac_key)
        return SAR_CAPTURE_INVALID;
    if (!req->plaintext || !req->ciphertext ||
        req->sample_size < (uint32_t)SAR_CANDIDATE_SIZE)
        return SAR_CAPTURE_INVALID;

    sar_gate_classify(gate_scratch, req->plaintext, req->ciphertext,
                      req->sample_size, &out->gate);
    if (!out->gate.candidate) {
        out->outcome = SAR_CAPTURE_SKIP_GATE;
        return SAR_CAPTURE_SKIP_GATE;
    }

    in.candidates = req->candidates;
    in.candidate_count = req->candidate_count;
    in.scan_buffer = req->scan_buffer;
    in.scan_length = req->scan_length;
    in.plaintext = req->plaintext;
    in.ciphertext = req->ciphertext;
    in.sample_size = req->sample_size;
    in.file_offset = req->file_offset;

    if (!sar_convict(&in, &out->verdict)) {
        out->outcome = SAR_CAPTURE_NO_CONVICTION;
        return SAR_CAPTURE_NO_CONVICTION;
    }

    sar_keystore_record_init(&out->record, mac_key, &out->verdict,
                             req->provenance_path, req->provenance_offset,
                             req->provenance_length,
                             req->plaintext, req->sample_size, req->file_offset);
    sar_capture_build_notify(&out->record, &out->notify);
    out->outcome = SAR_CAPTURE_CONVICTED;
    return SAR_CAPTURE_CONVICTED;
}
