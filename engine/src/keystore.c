#include "sar_keystore.h"
#include "sha256.h"
#include "eng_mem.h"

void sar_keystore_key_id(const uint8_t mac_key[SEMANTICS_AR_MAC_SIZE],
                         uint32_t algorithm, uint32_t mode,
                         uint32_t key_length, const uint8_t *key_bytes,
                         uint8_t out_id[SEMANTICS_AR_KEY_ID_SIZE]) {
    uint8_t msg[1 + 12 + SEMANTICS_AR_MAX_KEY_BYTES];
    size_t n = 0;
    msg[n++] = 0x01;
    for (int i = 0; i < 4; i++) msg[n++] = (uint8_t)(algorithm >> (8 * i));
    for (int i = 0; i < 4; i++) msg[n++] = (uint8_t)(mode >> (8 * i));
    for (int i = 0; i < 4; i++) msg[n++] = (uint8_t)(key_length >> (8 * i));
    uint32_t kl = key_length > SEMANTICS_AR_MAX_KEY_BYTES
                      ? SEMANTICS_AR_MAX_KEY_BYTES : key_length;
    sar_memcpy(msg + n, key_bytes, kl);
    n += kl;
    sar_hmac_sha256(mac_key, SEMANTICS_AR_MAC_SIZE, msg, n, out_id);
    sar_secure_zero(msg, sizeof(msg));
}

void sar_sample_tag(const uint8_t *data, uint32_t len,
                    uint8_t out[SEMANTICS_AR_MAC_SIZE]) {
    sar_sha256_ctx_t ctx;
    sar_sha256_init(&ctx);
    if (data && len)
        sar_sha256_update(&ctx, data, len);
    sar_sha256_final(&ctx, out);
}

void sar_keystore_record_init(semantics_ar_keystore_record_t *rec,
                              const uint8_t mac_key[SEMANTICS_AR_MAC_SIZE],
                              const sar_verdict_t *verdict,
                              const uint16_t *provenance_path,
                              uint64_t provenance_offset,
                              uint64_t provenance_length,
                              const uint8_t *sample,
                              uint32_t sample_len,
                              uint64_t sample_offset) {
    sar_memset(rec, 0, sizeof(*rec));
    rec->algorithm = verdict->algorithm;
    rec->mode = verdict->mode;
    rec->key_length = verdict->key_length;
    uint32_t kl = verdict->key_length > SEMANTICS_AR_MAX_KEY_BYTES
                      ? SEMANTICS_AR_MAX_KEY_BYTES : verdict->key_length;
    sar_memcpy(rec->key_bytes, verdict->key, kl);
    uint8_t ivl = verdict->iv_length > SEMANTICS_AR_IV_MAX
                      ? SEMANTICS_AR_IV_MAX : verdict->iv_length;
    sar_memcpy(rec->iv, verdict->iv, ivl);
    rec->iv_length = ivl;
    rec->ctr_layout_tag = verdict->ctr_layout_tag;
    rec->mode_params = verdict->mode_params;
    rec->provenance_offset = provenance_offset;
    rec->provenance_length = provenance_length;
    if (provenance_path) {
        for (int i = 0; i < SEMANTICS_AR_PROVENANCE_PATH_MAX; i++) {
            rec->provenance_path[i] = provenance_path[i];
            if (provenance_path[i] == 0) break;
        }
    }
    rec->sample_offset = sample_offset;
    if (sample && sample_len > 0) {
        uint32_t sl = sample_len > SEMANTICS_AR_SAMPLE_TAG_MAX
                          ? SEMANTICS_AR_SAMPLE_TAG_MAX : sample_len;
        rec->sample_length = sl;
        sar_sample_tag(sample, sl, rec->sample_tag);
    }
    sar_keystore_key_id(mac_key, rec->algorithm, rec->mode,
                        rec->key_length, rec->key_bytes, rec->key_id);
}

int sar_keystore_append(semantics_ar_keystore_record_t *records,
                        uint64_t *count, uint64_t capacity,
                        const semantics_ar_keystore_record_t *rec) {
    for (uint64_t i = 0; i < *count; i++) {
        if (sar_memcmp(records[i].key_id, rec->key_id, SEMANTICS_AR_KEY_ID_SIZE) == 0 &&
            records[i].provenance_offset == rec->provenance_offset &&
            sar_memcmp(records[i].provenance_path, rec->provenance_path,
                       sizeof(records[i].provenance_path)) == 0)
            return 0;
    }
    if (*count >= capacity)
        return SAR_KS_FULL;
    sar_memcpy(&records[*count], rec, sizeof(*rec));
    (*count)++;
    return 1;
}

void sar_keystore_mac_record(const uint8_t mac_key[SEMANTICS_AR_MAC_SIZE],
                             const semantics_ar_keystore_record_t *fields,
                             const uint8_t prev_mac[SEMANTICS_AR_MAC_SIZE],
                             uint8_t out_mac[SEMANTICS_AR_MAC_SIZE]) {
    uint8_t msg[sizeof(semantics_ar_keystore_record_t) + SEMANTICS_AR_MAC_SIZE];
    sar_memcpy(msg, fields, sizeof(*fields));
    sar_memcpy(msg + sizeof(*fields), prev_mac, SEMANTICS_AR_MAC_SIZE);
    sar_hmac_sha256(mac_key, SEMANTICS_AR_MAC_SIZE, msg, sizeof(msg), out_mac);
}

size_t sar_keystore_serialized_size(uint64_t count) {
    return sizeof(semantics_ar_keystore_header_t) +
           (size_t)count * sizeof(semantics_ar_keystore_disk_record_t);
}

int sar_keystore_serialize(const uint8_t mac_key[SEMANTICS_AR_MAC_SIZE],
                           const semantics_ar_keystore_record_t *records,
                           uint64_t count, uint64_t generation,
                           uint8_t *out_buf, size_t out_cap, size_t *out_len) {
    size_t need = sar_keystore_serialized_size(count);
    if (out_len) *out_len = need;
    if (out_cap < need)
        return SAR_KS_BUFFER_TOO_SMALL;

    semantics_ar_keystore_header_t hdr;
    sar_memset(&hdr, 0, sizeof(hdr));
    hdr.magic = SEMANTICS_AR_KEYSTORE_MAGIC;
    hdr.protocol_version = SEMANTICS_AR_KEYSTORE_VERSION;
    hdr.record_count = count;
    hdr.generation = generation;

    uint8_t prev[SEMANTICS_AR_MAC_SIZE];
    sar_memset(prev, 0, SEMANTICS_AR_MAC_SIZE);

    uint8_t *p = out_buf + sizeof(hdr);
    for (uint64_t i = 0; i < count; i++) {
        semantics_ar_keystore_disk_record_t disk;
        sar_memcpy(&disk.fields, &records[i], sizeof(records[i]));
        sar_keystore_mac_record(mac_key, &records[i], prev, disk.mac);
        sar_memcpy(prev, disk.mac, SEMANTICS_AR_MAC_SIZE);
        sar_memcpy(p, &disk, sizeof(disk));
        p += sizeof(disk);
    }
    sar_memcpy(hdr.head_mac, prev, SEMANTICS_AR_MAC_SIZE);
    sar_memcpy(out_buf, &hdr, sizeof(hdr));
    return SAR_KS_OK;
}

int sar_keystore_verify(const uint8_t *buf, size_t len,
                        const uint8_t mac_key[SEMANTICS_AR_MAC_SIZE],
                        const sar_keystore_anchor_t *anchor,
                        uint64_t *out_count) {
    if (!buf || len < sizeof(semantics_ar_keystore_header_t))
        return SAR_KS_TRUNCATED;

    semantics_ar_keystore_header_t hdr;
    sar_memcpy(&hdr, buf, sizeof(hdr));
    if (hdr.magic != SEMANTICS_AR_KEYSTORE_MAGIC)
        return SAR_KS_BAD_MAGIC;
    if (hdr.protocol_version != SEMANTICS_AR_KEYSTORE_VERSION)
        return SAR_KS_BAD_VERSION;

    size_t need = sar_keystore_serialized_size(hdr.record_count);
    if (len < need)
        return SAR_KS_TRUNCATED;
    if (len != need)
        return SAR_KS_COUNT_MISMATCH;

    uint8_t prev[SEMANTICS_AR_MAC_SIZE];
    sar_memset(prev, 0, SEMANTICS_AR_MAC_SIZE);

    const uint8_t *p = buf + sizeof(hdr);
    for (uint64_t i = 0; i < hdr.record_count; i++) {
        semantics_ar_keystore_disk_record_t disk;
        sar_memcpy(&disk, p, sizeof(disk));
        uint8_t mac[SEMANTICS_AR_MAC_SIZE];
        sar_keystore_mac_record(mac_key, &disk.fields, prev, mac);
        if (!sar_ct_equal(mac, disk.mac, SEMANTICS_AR_MAC_SIZE))
            return SAR_KS_RECORD_MAC;
        sar_memcpy(prev, disk.mac, SEMANTICS_AR_MAC_SIZE);
        p += sizeof(disk);
    }

    if (!sar_ct_equal(prev, hdr.head_mac, SEMANTICS_AR_MAC_SIZE))
        return SAR_KS_RECORD_MAC;

    if (anchor && anchor->present) {
        if (hdr.generation != anchor->generation)
            return SAR_KS_ROLLBACK;
        if (!sar_ct_equal(hdr.head_mac, anchor->head_mac, SEMANTICS_AR_MAC_SIZE))
            return SAR_KS_ROLLBACK;
    }

    if (out_count) *out_count = hdr.record_count;
    return SAR_KS_OK;
}
