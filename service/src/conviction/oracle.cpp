#include "key_capture.h"
#include "oracle_battery.h"
#include "mode_verify.h"
#include <tlhelp32.h>
#include <stdlib.h>
#include <string.h>

typedef LONG (NTAPI *NtSuspendProcess_t)(HANDLE);
typedef LONG (NTAPI *NtResumeProcess_t)(HANDLE);

static NtSuspendProcess_t pfnNtSuspendProcess = NULL;
static NtResumeProcess_t  pfnNtResumeProcess = NULL;

#define ORACLE_MEM_SCAN_CHUNK   (1u << 20)
#define ORACLE_MEM_SCAN_MAX     ((SIZE_T)64u * 1024u * 1024u)

void svc_oracle_init(void) {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        pfnNtSuspendProcess = (NtSuspendProcess_t)GetProcAddress(ntdll, "NtSuspendProcess");
        pfnNtResumeProcess = (NtResumeProcess_t)GetProcAddress(ntdll, "NtResumeProcess");
    }
    BCryptOpenAlgorithmProvider(&g_hAesEcb, BCRYPT_AES_ALGORITHM, NULL, 0);
    if (g_hAesEcb) {
        WCHAR mode[] = BCRYPT_CHAIN_MODE_ECB;
        BCryptSetProperty(g_hAesEcb, BCRYPT_CHAINING_MODE, (PUCHAR)mode, sizeof(mode), 0);
    }
    BCryptOpenAlgorithmProvider(&g_h3DesEcb, BCRYPT_3DES_ALGORITHM, NULL, 0);
    if (g_h3DesEcb) {
        WCHAR mode[] = BCRYPT_CHAIN_MODE_ECB;
        BCryptSetProperty(g_h3DesEcb, BCRYPT_CHAINING_MODE, (PUCHAR)mode, sizeof(mode), 0);
    }
}

void svc_oracle_cleanup(void) {
    if (g_hAesEcb) { BCryptCloseAlgorithmProvider(g_hAesEcb, 0); g_hAesEcb = NULL; }
    if (g_h3DesEcb) { BCryptCloseAlgorithmProvider(g_h3DesEcb, 0); g_h3DesEcb = NULL; }
}

static uint32_t extract_register_candidates(DWORD pid, oracle_candidate_t *cands,
                                            uint32_t max_cands) {
    uint32_t count = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;
    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid)
            continue;
        HANDLE ht = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                               FALSE, te.th32ThreadID);
        if (!ht)
            continue;
        __declspec(align(16)) CONTEXT ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.ContextFlags = CONTEXT_FLOATING_POINT;
        if (GetThreadContext(ht, &ctx)) {
            for (int r = 0; r < 16 && count < max_cands; r++) {
                memcpy(cands[count].value, &ctx.FltSave.XmmRegisters[r], 16);
                cands[count].thread_id = te.th32ThreadID;
                cands[count].register_index = (uint32_t)r;
                count++;
            }
        }
        CloseHandle(ht);
        if (count >= max_cands)
            break;
    }
    CloseHandle(snap);
    return count;
}

static int oracle_register_phase(const oracle_candidate_t *cands, uint32_t count,
                                 DWORD thread_id,
                                 const uint8_t *plaintext, const uint8_t *ciphertext,
                                 uint32_t sample_size, uint64_t file_offset,
                                 oracle_result_t *result) {
    if (oracle_try_register_block_keys(cands, count, plaintext, ciphertext,
                                       sample_size, file_offset, result))
        return 1;
    if (oracle_try_register_stream_keys(cands, count, thread_id, plaintext, ciphertext,
                                        sample_size, file_offset, result))
        return 1;
    return 0;
}

static int oracle_scan_phase(HANDLE hProc,
                             const uint8_t *plaintext, const uint8_t *ciphertext,
                             uint32_t sample_size, uint64_t file_offset,
                             oracle_result_t *result) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uint8_t *addr = (uint8_t *)si.lpMinimumApplicationAddress;
    uint8_t *maxAddr = (uint8_t *)si.lpMaximumApplicationAddress;

    uint8_t *buf = (uint8_t *)malloc(ORACLE_MEM_SCAN_CHUNK);
    if (!buf)
        return 0;

    SIZE_T scanned = 0;
    int confirmed = 0;

    while (addr < maxAddr && !confirmed && scanned < ORACLE_MEM_SCAN_MAX) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQueryEx(hProc, addr, &mbi, sizeof(mbi)) == 0)
            break;

        BOOL readable = (mbi.State == MEM_COMMIT) &&
            (mbi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_EXECUTE_READ |
                            PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY)) &&
            !(mbi.Protect & PAGE_GUARD) &&
            (mbi.Type == MEM_PRIVATE);

        if (readable) {
            uint8_t *region = (uint8_t *)mbi.BaseAddress;
            SIZE_T remaining = mbi.RegionSize;
            while (remaining > 0 && !confirmed && scanned < ORACLE_MEM_SCAN_MAX) {
                SIZE_T toRead = remaining < ORACLE_MEM_SCAN_CHUNK ? remaining : ORACLE_MEM_SCAN_CHUNK;
                SIZE_T br = 0;
                if (ReadProcessMemory(hProc, region, buf, toRead, &br) && br >= 16) {
                    if (oracle_try_buffer_keys(buf, br, plaintext, ciphertext,
                                               sample_size, file_offset, result))
                        confirmed = 1;
                    scanned += br;
                }
                region += toRead;
                remaining -= toRead;
            }
        }

        uint8_t *next = (uint8_t *)mbi.BaseAddress + mbi.RegionSize;
        if (next <= addr)
            break;
        addr = next;
    }

    free(buf);
    return confirmed;
}

int svc_oracle_test(DWORD target_pid, DWORD thread_id,
                    const uint8_t *plaintext, const uint8_t *ciphertext,
                    uint32_t sample_size, uint64_t file_offset,
                    oracle_result_t *result) {
    if (!pfnNtSuspendProcess || !pfnNtResumeProcess || !g_hAesEcb || !g_h3DesEcb)
        return 0;
    if (sample_size < 16)
        return 0;

    memset(result, 0, sizeof(*result));

    HANDLE hProc = OpenProcess(
        PROCESS_SUSPEND_RESUME | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
        FALSE, target_pid);
    if (!hProc)
        return 0;

    pfnNtSuspendProcess(hProc);

    oracle_candidate_t *cands = (oracle_candidate_t *)calloc(
        ORACLE_MAX_CANDIDATES, sizeof(oracle_candidate_t));
    if (!cands) {
        pfnNtResumeProcess(hProc);
        CloseHandle(hProc);
        return 0;
    }

    uint32_t count = extract_register_candidates(target_pid, cands, ORACLE_MAX_CANDIDATES);

    int confirmed = oracle_register_phase(cands, count, thread_id,
                                          plaintext, ciphertext,
                                          sample_size, file_offset, result);

    if (!confirmed)
        confirmed = oracle_scan_phase(hProc, plaintext, ciphertext,
                                      sample_size, file_offset, result);

    free(cands);
    pfnNtResumeProcess(hProc);
    CloseHandle(hProc);

    return confirmed;
}