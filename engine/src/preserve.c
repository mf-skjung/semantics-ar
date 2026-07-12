#include "sar_preserve.h"
#include "sha256.h"
#include "eng_mem.h"

static int sar_pres_path_eq(const uint16_t *a, const uint16_t *b) {
    return sar_memcmp(a, b, SEMANTICS_AR_PROVENANCE_PATH_MAX * sizeof(uint16_t)) == 0;
}

static int sar_pres_overlap(uint64_t a_off, uint64_t a_len,
                            uint64_t b_off, uint64_t b_len) {
    return a_off < b_off + b_len && b_off < a_off + a_len;
}

static int sar_pres_contains(uint64_t outer_off, uint64_t outer_len,
                             uint64_t inner_off, uint64_t inner_len) {
    return outer_off <= inner_off &&
           inner_off + inner_len <= outer_off + outer_len;
}

static void sar_pres_mac_record(const uint8_t mac_key[SEMANTICS_AR_MAC_SIZE],
                                const sar_preserve_record_t *fields,
                                const uint8_t prev_mac[SEMANTICS_AR_MAC_SIZE],
                                uint8_t out_mac[SEMANTICS_AR_MAC_SIZE]) {
    uint8_t msg[sizeof(sar_preserve_record_t) + SEMANTICS_AR_MAC_SIZE];
    sar_memcpy(msg, fields, sizeof(*fields));
    sar_memcpy(msg + sizeof(*fields), prev_mac, SEMANTICS_AR_MAC_SIZE);
    sar_hmac_sha256(mac_key, SEMANTICS_AR_MAC_SIZE, msg, sizeof(msg), out_mac);
}

static void sar_pres_mac_head(const uint8_t mac_key[SEMANTICS_AR_MAC_SIZE],
                              uint64_t generation, uint64_t record_count,
                              uint8_t out_mac[SEMANTICS_AR_MAC_SIZE]) {
    uint8_t msg[sizeof(uint64_t) * 2];
    sar_memcpy(msg, &generation, sizeof(generation));
    sar_memcpy(msg + sizeof(generation), &record_count, sizeof(record_count));
    sar_hmac_sha256(mac_key, SEMANTICS_AR_MAC_SIZE, msg, sizeof(msg), out_mac);
}

void sar_preserve_content_tag(const uint8_t *plaintext, uint64_t len,
                              uint8_t out[SEMANTICS_AR_MAC_SIZE]) {
    sar_sha256_ctx_t ctx;
    sar_sha256_init(&ctx);
    if (plaintext && len)
        sar_sha256_update(&ctx, plaintext, (size_t)len);
    sar_sha256_final(&ctx, out);
}

void sar_preserve_record_init(sar_preserve_record_t *rec,
                              const uint16_t *provenance_path,
                              uint64_t provenance_offset,
                              uint64_t provenance_length,
                              uint64_t capture_time,
                              uint64_t payload_offset,
                              uint64_t payload_length,
                              uint64_t actor_id,
                              const uint8_t *iv, uint8_t iv_length,
                              const uint8_t *plaintext, uint64_t plaintext_length) {
    sar_memset(rec, 0, sizeof(*rec));
    if (provenance_path) {
        for (int i = 0; i < SEMANTICS_AR_PROVENANCE_PATH_MAX; i++) {
            rec->provenance_path[i] = provenance_path[i];
            if (provenance_path[i] == 0) break;
        }
    }
    rec->provenance_offset = provenance_offset;
    rec->provenance_length = provenance_length;
    rec->capture_time = capture_time;
    rec->payload_offset = payload_offset;
    rec->payload_length = payload_length;
    rec->actor_id = actor_id;
    rec->state = SAR_PRESERVE_PROBATION;
    uint8_t ivl = iv_length > SEMANTICS_AR_IV_MAX ? SEMANTICS_AR_IV_MAX : iv_length;
    if (iv) sar_memcpy(rec->iv, iv, ivl);
    rec->iv_length = ivl;
    sar_preserve_content_tag(plaintext, plaintext_length, rec->content_tag);
}

int sar_preserve_covered(const sar_preserve_record_t *records, uint64_t count,
                         const uint16_t *provenance_path,
                         uint64_t offset, uint64_t length) {
    for (uint64_t i = 0; i < count; i++) {
        if (sar_pres_path_eq(records[i].provenance_path, provenance_path) &&
            sar_pres_overlap(records[i].provenance_offset,
                             records[i].provenance_length, offset, length))
            return 1;
    }
    return 0;
}

int sar_preserve_first_gap(const sar_preserve_record_t *records, uint64_t count,
                           const uint16_t *provenance_path,
                           uint64_t offset, uint64_t length,
                           uint64_t *gap_offset, uint64_t *gap_length) {
    uint64_t cursor = offset;
    uint64_t end = offset + length;
    while (cursor < end) {
        uint64_t cover_end = cursor;
        int covered = 0;
        for (uint64_t i = 0; i < count; i++) {
            if (!sar_pres_path_eq(records[i].provenance_path, provenance_path))
                continue;
            uint64_t ro = records[i].provenance_offset;
            uint64_t re = ro + records[i].provenance_length;
            if (ro <= cursor && cursor < re) {
                covered = 1;
                if (re > cover_end)
                    cover_end = re;
            }
        }
        if (covered) {
            cursor = cover_end;
            continue;
        }
        uint64_t gap_end = end;
        for (uint64_t i = 0; i < count; i++) {
            if (!sar_pres_path_eq(records[i].provenance_path, provenance_path))
                continue;
            uint64_t ro = records[i].provenance_offset;
            if (ro > cursor && ro < gap_end)
                gap_end = ro;
        }
        *gap_offset = cursor;
        *gap_length = gap_end - cursor;
        return 1;
    }
    return 0;
}

int sar_preserve_append(sar_preserve_record_t *records,
                        uint64_t *count, uint64_t capacity,
                        const sar_preserve_record_t *rec) {
    if (sar_preserve_covered(records, *count, rec->provenance_path,
                             rec->provenance_offset, rec->provenance_length))
        return 0;
    if (*count >= capacity)
        return SAR_PRES_FULL;
    sar_memcpy(&records[*count], rec, sizeof(*rec));
    (*count)++;
    return 1;
}

uint64_t sar_preserve_promote(sar_preserve_record_t *records, uint64_t count,
                              uint64_t actor_id) {
    uint64_t promoted = 0;
    for (uint64_t i = 0; i < count; i++) {
        if (records[i].state == SAR_PRESERVE_PROBATION &&
            records[i].actor_id == actor_id) {
            records[i].state = SAR_PRESERVE_PROTECTED;
            promoted++;
        }
    }
    return promoted;
}

uint64_t sar_preserve_reconcile(sar_preserve_record_t *records, uint64_t *count,
                                const uint16_t *provenance_path,
                                uint64_t key_offset, uint64_t key_length) {
    uint64_t w = 0, removed = 0;
    for (uint64_t r = 0; r < *count; r++) {
        if (sar_pres_path_eq(records[r].provenance_path, provenance_path) &&
            sar_pres_contains(key_offset, key_length,
                              records[r].provenance_offset,
                              records[r].provenance_length)) {
            removed++;
            continue;
        }
        if (w != r) sar_memcpy(&records[w], &records[r], sizeof(records[w]));
        w++;
    }
    *count = w;
    return removed;
}

uint64_t sar_preserve_total_bytes(const sar_preserve_record_t *records,
                                  uint64_t count) {
    uint64_t total = 0;
    for (uint64_t i = 0; i < count; i++)
        total += records[i].payload_length;
    return total;
}

uint64_t sar_preserve_protected_bytes(const sar_preserve_record_t *records,
                                      uint64_t count) {
    uint64_t total = 0;
    for (uint64_t i = 0; i < count; i++)
        if (records[i].state == SAR_PRESERVE_PROTECTED)
            total += records[i].payload_length;
    return total;
}

uint64_t sar_preserve_probation_bytes(const sar_preserve_record_t *records,
                                      uint64_t count) {
    uint64_t total = 0;
    for (uint64_t i = 0; i < count; i++)
        if (records[i].state == SAR_PRESERVE_PROBATION)
            total += records[i].payload_length;
    return total;
}

uint64_t sar_preserve_evict_aged(sar_preserve_record_t *records, uint64_t *count,
                                 uint64_t now, uint64_t retention) {
    uint64_t w = 0, removed = 0;
    for (uint64_t r = 0; r < *count; r++) {
        if (now > records[r].capture_time &&
            now - records[r].capture_time >= retention) {
            removed++;
            continue;
        }
        if (w != r) sar_memcpy(&records[w], &records[r], sizeof(records[w]));
        w++;
    }
    *count = w;
    return removed;
}

uint64_t sar_preserve_evict_probation_oldest(sar_preserve_record_t *records,
                                             uint64_t *count,
                                             uint64_t probation_cap_bytes) {
    uint64_t removed = 0;
    for (;;) {
        if (sar_preserve_probation_bytes(records, *count) <= probation_cap_bytes)
            break;
        uint64_t oldest = *count;
        for (uint64_t i = 0; i < *count; i++) {
            if (records[i].state != SAR_PRESERVE_PROBATION)
                continue;
            if (oldest == *count ||
                records[i].capture_time < records[oldest].capture_time)
                oldest = i;
        }
        if (oldest == *count)
            break;
        for (uint64_t i = oldest + 1; i < *count; i++)
            sar_memcpy(&records[i - 1], &records[i], sizeof(records[i]));
        (*count)--;
        removed++;
    }
    return removed;
}

int sar_preserve_would_exceed(const sar_preserve_record_t *records, uint64_t count,
                              uint64_t capacity_bytes, uint64_t incoming_bytes) {
    return sar_preserve_total_bytes(records, count) + incoming_bytes > capacity_bytes;
}

int sar_preserve_verify_extract(const sar_preserve_record_t *rec,
                                const uint16_t *target_path,
                                uint64_t target_offset, uint64_t target_length,
                                const uint8_t *decrypted_region, uint64_t region_length,
                                uint64_t *inner_offset) {
    if (!rec || !target_path || !decrypted_region || !inner_offset || target_length == 0)
        return SAR_PRES_INVALID_ARG;
    if (!sar_pres_path_eq(rec->provenance_path, target_path) ||
        region_length != rec->provenance_length ||
        !sar_pres_contains(rec->provenance_offset, rec->provenance_length,
                           target_offset, target_length))
        return SAR_PRES_RESTORE_MISMATCH;
    uint8_t tag[SEMANTICS_AR_MAC_SIZE];
    sar_preserve_content_tag(decrypted_region, region_length, tag);
    if (!sar_ct_equal(tag, rec->content_tag, SEMANTICS_AR_MAC_SIZE))
        return SAR_PRES_RESTORE_MISMATCH;
    *inner_offset = target_offset - rec->provenance_offset;
    return SAR_PRES_OK;
}

size_t sar_preserve_serialized_size(uint64_t count) {
    return sizeof(sar_preserve_header_t) +
           (size_t)count * sizeof(sar_preserve_disk_record_t);
}

int sar_preserve_serialize(const uint8_t mac_key[SEMANTICS_AR_MAC_SIZE],
                           const sar_preserve_record_t *records,
                           uint64_t count, uint64_t generation,
                           uint8_t *out_buf, size_t out_cap, size_t *out_len) {
    size_t need = sar_preserve_serialized_size(count);
    if (out_len) *out_len = need;
    if (out_cap < need)
        return SAR_PRES_BUFFER_TOO_SMALL;

    sar_preserve_header_t hdr;
    sar_memset(&hdr, 0, sizeof(hdr));
    hdr.magic = SEMANTICS_AR_PRESERVE_MAGIC;
    hdr.record_count = count;
    hdr.generation = generation;

    uint8_t prev[SEMANTICS_AR_MAC_SIZE];
    sar_pres_mac_head(mac_key, generation, count, prev);

    uint8_t *p = out_buf + sizeof(hdr);
    for (uint64_t i = 0; i < count; i++) {
        sar_preserve_disk_record_t disk;
        sar_memcpy(&disk.fields, &records[i], sizeof(records[i]));
        sar_pres_mac_record(mac_key, &records[i], prev, disk.mac);
        sar_memcpy(prev, disk.mac, SEMANTICS_AR_MAC_SIZE);
        sar_memcpy(p, &disk, sizeof(disk));
        p += sizeof(disk);
    }
    sar_memcpy(hdr.head_mac, prev, SEMANTICS_AR_MAC_SIZE);
    sar_memcpy(out_buf, &hdr, sizeof(hdr));
    return SAR_PRES_OK;
}

int sar_preserve_verify(const uint8_t *buf, size_t len,
                        const uint8_t mac_key[SEMANTICS_AR_MAC_SIZE],
                        const sar_keystore_anchor_t *anchor,
                        uint64_t *out_count) {
    if (!buf || len < sizeof(sar_preserve_header_t))
        return SAR_PRES_TRUNCATED;

    sar_preserve_header_t hdr;
    sar_memcpy(&hdr, buf, sizeof(hdr));
    if (hdr.magic != SEMANTICS_AR_PRESERVE_MAGIC)
        return SAR_PRES_BAD_MAGIC;

    if (hdr.record_count >
        (len - sizeof(sar_preserve_header_t)) / sizeof(sar_preserve_disk_record_t))
        return SAR_PRES_TRUNCATED;
    size_t need = sar_preserve_serialized_size(hdr.record_count);
    if (len != need)
        return SAR_PRES_COUNT_MISMATCH;

    uint8_t prev[SEMANTICS_AR_MAC_SIZE];
    sar_pres_mac_head(mac_key, hdr.generation, hdr.record_count, prev);

    const uint8_t *p = buf + sizeof(hdr);
    for (uint64_t i = 0; i < hdr.record_count; i++) {
        sar_preserve_disk_record_t disk;
        sar_memcpy(&disk, p, sizeof(disk));
        uint8_t mac[SEMANTICS_AR_MAC_SIZE];
        sar_pres_mac_record(mac_key, &disk.fields, prev, mac);
        if (!sar_ct_equal(mac, disk.mac, SEMANTICS_AR_MAC_SIZE))
            return SAR_PRES_RECORD_MAC;
        sar_memcpy(prev, disk.mac, SEMANTICS_AR_MAC_SIZE);
        p += sizeof(disk);
    }

    if (!sar_ct_equal(prev, hdr.head_mac, SEMANTICS_AR_MAC_SIZE))
        return SAR_PRES_RECORD_MAC;

    if (anchor && anchor->present) {
        if (hdr.generation < anchor->generation)
            return SAR_PRES_ROLLBACK;
        if (hdr.generation == anchor->generation
            && !sar_ct_equal(hdr.head_mac, anchor->head_mac, SEMANTICS_AR_MAC_SIZE))
            return SAR_PRES_ROLLBACK;
    }

    if (out_count) *out_count = hdr.record_count;
    return SAR_PRES_OK;
}
