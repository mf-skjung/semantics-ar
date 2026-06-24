#include "sar_keystore_mgr.h"
#include "eng_mem.h"

static sar_ksm_status_t ksm_map_verify(int rc) {
    switch (rc) {
    case SAR_KS_OK:
        return SAR_KSM_OK;
    default:
        return SAR_KSM_CORRUPT;
    }
}

sar_ksm_status_t sar_ksm_load(const uint8_t *buf, size_t len,
                              const uint8_t mac_key[SEMANTICS_AR_MAC_SIZE],
                              const sar_keystore_anchor_t *anchor,
                              semantics_ar_keystore_record_t *records_out,
                              uint64_t records_capacity,
                              uint64_t *count_out,
                              sar_keystore_anchor_t *anchor_out,
                              sar_ksm_load_result_t *result) {
    if (!mac_key || !records_out || !count_out || !anchor_out || !result)
        return SAR_KSM_INVALID_ARG;

    *count_out = 0;
    result->generation = 0;
    result->anchor_advanced = 0;
    anchor_out->present = 0;
    anchor_out->generation = 0;
    sar_memset(anchor_out->head_mac, 0, SEMANTICS_AR_MAC_SIZE);

    if (!buf || len == 0) {
        if (anchor && anchor->present && anchor->generation > 0)
            return SAR_KSM_ROLLBACK;
        return SAR_KSM_EMPTY;
    }

    uint64_t count = 0;
    sar_ksm_status_t s = ksm_map_verify(
        sar_keystore_verify(buf, len, mac_key, NULL, &count));
    if (s != SAR_KSM_OK)
        return s;
    if (count > records_capacity)
        return SAR_KSM_OVERFLOW;

    semantics_ar_keystore_header_t hdr;
    sar_memcpy(&hdr, buf, sizeof(hdr));

    int advanced;
    if (!anchor || !anchor->present) {
        advanced = 1;
    } else if (hdr.generation < anchor->generation) {
        return SAR_KSM_ROLLBACK;
    } else if (hdr.generation == anchor->generation) {
        if (!sar_ct_equal(hdr.head_mac, anchor->head_mac, SEMANTICS_AR_MAC_SIZE))
            return SAR_KSM_ROLLBACK;
        advanced = 0;
    } else {
        advanced = 1;
    }

    const uint8_t *p = buf + sizeof(hdr);
    for (uint64_t i = 0; i < count; i++) {
        semantics_ar_keystore_disk_record_t disk;
        sar_memcpy(&disk, p, sizeof(disk));
        sar_memcpy(&records_out[i], &disk.fields, sizeof(disk.fields));
        p += sizeof(disk);
    }

    anchor_out->present = 1;
    anchor_out->generation = hdr.generation;
    sar_memcpy(anchor_out->head_mac, hdr.head_mac, SEMANTICS_AR_MAC_SIZE);

    *count_out = count;
    result->generation = hdr.generation;
    result->anchor_advanced = advanced;
    return SAR_KSM_OK;
}

sar_ksm_status_t sar_ksm_persist(const uint8_t mac_key[SEMANTICS_AR_MAC_SIZE],
                                 const semantics_ar_keystore_record_t *records,
                                 uint64_t count, uint64_t prev_generation,
                                 uint8_t *out_buf, size_t out_cap, size_t *out_len,
                                 sar_keystore_anchor_t *anchor_out) {
    if (!mac_key || (!records && count > 0) || !out_buf || !out_len || !anchor_out)
        return SAR_KSM_INVALID_ARG;

    uint64_t new_gen = prev_generation + 1;
    int rc = sar_keystore_serialize(mac_key, records, count, new_gen,
                                    out_buf, out_cap, out_len);
    if (rc == SAR_KS_BUFFER_TOO_SMALL)
        return SAR_KSM_BUFFER_TOO_SMALL;
    if (rc != SAR_KS_OK)
        return SAR_KSM_CORRUPT;

    semantics_ar_keystore_header_t hdr;
    sar_memcpy(&hdr, out_buf, sizeof(hdr));

    anchor_out->present = 1;
    anchor_out->generation = new_gen;
    sar_memcpy(anchor_out->head_mac, hdr.head_mac, SEMANTICS_AR_MAC_SIZE);
    return SAR_KSM_OK;
}
