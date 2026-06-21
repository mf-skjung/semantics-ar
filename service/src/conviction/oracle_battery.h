#ifndef SEMANTICS_AR_ORACLE_BATTERY_H
#define SEMANTICS_AR_ORACLE_BATTERY_H

#include <windows.h>
#include <bcrypt.h>
#include "key_capture.h"

extern BCRYPT_ALG_HANDLE g_hAesEcb;
extern BCRYPT_ALG_HANDLE g_h3DesEcb;

int oracle_try_register_block_keys(
    const oracle_candidate_t *cands, uint32_t count,
    const uint8_t *pt, const uint8_t *ct,
    uint32_t sample_size, uint64_t file_offset,
    oracle_result_t *result);

int oracle_try_register_stream_keys(
    const oracle_candidate_t *cands, uint32_t count, DWORD writing_tid,
    const uint8_t *pt, const uint8_t *ct,
    uint32_t sample_size, uint64_t file_offset,
    oracle_result_t *result);

int oracle_try_buffer_keys(
    const uint8_t *buf, SIZE_T buf_len,
    const uint8_t *pt, const uint8_t *ct,
    uint32_t sample_size, uint64_t file_offset,
    oracle_result_t *result);

#endif