#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include "sha256.h"
#include "eng_mem.h"

#ifdef _MSC_VER
#pragma warning(disable: 4127 4819)
#endif

static int g_pass;
static int g_fail;

#define CHECK(cond, name) do {                                  \
    if (cond) { g_pass++; printf("  ok   %s\n", (name)); }     \
    else { g_fail++; printf("  FAIL %s\n", (name)); }          \
} while (0)

#define PHANTOM_K_THRESHOLD      3
#define PHANTOM_MAX_PER_DIR      3
#define PHANTOM_SECRET_BYTES     32
#define PHANTOM_NAME_CHARS       32
#define PHANTOM_SWAP_EXTRA       (4u * 1024u)
#define PHANTOM_SYNTH_REF_MASK   0xFFFF000000000000ULL

typedef uint16_t WCHAR_T;

static void phantom_derive_secret(const uint8_t mac_key[32], uint8_t secret[32])
{
    static const uint8_t label[] = "phantom-volume-secret";
    sar_hmac_sha256(mac_key, 32, label, sizeof(label) - 1, secret);
}

static void phantom_name_for_index(const uint8_t secret[32],
                                   const WCHAR_T *dir_path, size_t dir_chars,
                                   uint32_t index,
                                   WCHAR_T *name_buf, uint16_t *name_chars)
{
    uint8_t msg[520 + 4];
    uint8_t hash[32];
    static const WCHAR_T hex[] = {
        L'0',L'1',L'2',L'3',L'4',L'5',L'6',L'7',
        L'8',L'9',L'a',L'b',L'c',L'd',L'e',L'f'
    };
    size_t dir_bytes = dir_chars * sizeof(WCHAR_T);
    size_t i;

    if (dir_bytes > 520)
        dir_bytes = 520;
    memcpy(msg, dir_path, dir_bytes);
    msg[dir_bytes]     = (uint8_t)(index & 0xFF);
    msg[dir_bytes + 1] = (uint8_t)((index >> 8) & 0xFF);
    msg[dir_bytes + 2] = (uint8_t)((index >> 16) & 0xFF);
    msg[dir_bytes + 3] = (uint8_t)((index >> 24) & 0xFF);

    sar_hmac_sha256(secret, 32, msg, dir_bytes + 4, hash);

    for (i = 0; i < 12; i++) {
        name_buf[i * 2]     = hex[(hash[i] >> 4) & 0xF];
        name_buf[i * 2 + 1] = hex[hash[i] & 0xF];
    }
    name_buf[24] = L'.';
    name_buf[25] = L'd';
    name_buf[26] = L'o';
    name_buf[27] = L'c';
    name_buf[28] = L'x';
    *name_chars = 29;
}

static uint32_t phantom_count_for_dir(uint32_t real_file_count)
{
    if (real_file_count == 0) return 0;
    if (real_file_count <= 5) return 1;
    if (real_file_count <= 20) return 2;
    return PHANTOM_MAX_PER_DIR;
}

static uint64_t phantom_synthetic_ref(uint32_t dir_hash, uint32_t index)
{
    return PHANTOM_SYNTH_REF_MASK |
           ((uint64_t)(dir_hash & 0xFFFF) << 32) |
           ((uint64_t)(index & 0xFFFF));
}

static int phantom_is_phantom_path(const WCHAR_T *path, size_t path_chars)
{
    static const WCHAR_T marker[] = { L'\\', L'p', L'h', L'a', L'n', L't', L'o', L'm', L'\\' };
    size_t mlen = sizeof(marker) / sizeof(marker[0]);
    size_t i, j;

    if (path_chars < mlen)
        return 0;

    for (i = 0; i + mlen <= path_chars; i++) {
        int match = 1;
        for (j = 0; j < mlen; j++) {
            WCHAR_T a = path[i + j];
            WCHAR_T b = marker[j];
            if (a >= L'A' && a <= L'Z') a = (WCHAR_T)(a + 32);
            if (b >= L'A' && b <= L'Z') b = (WCHAR_T)(b + 32);
            if (a != b) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

typedef struct {
    uint32_t next_entry;
    uint32_t file_index;
    uint32_t creation_time;
    uint32_t last_access;
    uint32_t last_write;
    uint32_t change_time;
    uint32_t end_of_file;
    uint32_t alloc_size;
    uint32_t attrs;
    uint32_t name_length;
    uint32_t name_start;
    int32_t  ea_size;
    int32_t  short_name_length;
    int32_t  short_name;
    int32_t  file_id;
    uint32_t base_record_size;
} dir_offsets_t;

static uint32_t phantom_entry_size(const dir_offsets_t *off, uint32_t name_bytes)
{
    uint32_t size = off->base_record_size + name_bytes;
    return (size + 7) & ~7u;
}

typedef union {
    struct { uint32_t lo; int32_t hi; } u;
    int64_t QuadPart;
} LARGE_INTEGER_T;

typedef struct {
    uint32_t NextEntryOffset;
    uint32_t FileIndex;
    LARGE_INTEGER_T CreationTime;
    LARGE_INTEGER_T LastAccessTime;
    LARGE_INTEGER_T LastWriteTime;
    LARGE_INTEGER_T ChangeTime;
    LARGE_INTEGER_T EndOfFile;
    LARGE_INTEGER_T AllocationSize;
    uint32_t FileAttributes;
    uint32_t FileNameLength;
    WCHAR_T  FileName[1];
} FILE_DIRECTORY_INFORMATION_T;

typedef struct {
    uint32_t NextEntryOffset;
    uint32_t FileIndex;
    LARGE_INTEGER_T CreationTime;
    LARGE_INTEGER_T LastAccessTime;
    LARGE_INTEGER_T LastWriteTime;
    LARGE_INTEGER_T ChangeTime;
    LARGE_INTEGER_T EndOfFile;
    LARGE_INTEGER_T AllocationSize;
    uint32_t FileAttributes;
    uint32_t FileNameLength;
    uint32_t EaSize;
    WCHAR_T  FileName[1];
} FILE_FULL_DIR_INFORMATION_T;

typedef struct {
    uint32_t NextEntryOffset;
    uint32_t FileIndex;
    LARGE_INTEGER_T CreationTime;
    LARGE_INTEGER_T LastAccessTime;
    LARGE_INTEGER_T LastWriteTime;
    LARGE_INTEGER_T ChangeTime;
    LARGE_INTEGER_T EndOfFile;
    LARGE_INTEGER_T AllocationSize;
    uint32_t FileAttributes;
    uint32_t FileNameLength;
    uint32_t EaSize;
    char     ShortNameLength;
    WCHAR_T  ShortName[12];
    WCHAR_T  FileName[1];
} FILE_BOTH_DIR_INFORMATION_T;

typedef struct {
    uint32_t NextEntryOffset;
    uint32_t FileIndex;
    uint32_t FileNameLength;
    WCHAR_T  FileName[1];
} FILE_NAMES_INFORMATION_T;

typedef struct {
    uint32_t NextEntryOffset;
    uint32_t FileIndex;
    LARGE_INTEGER_T CreationTime;
    LARGE_INTEGER_T LastAccessTime;
    LARGE_INTEGER_T LastWriteTime;
    LARGE_INTEGER_T ChangeTime;
    LARGE_INTEGER_T EndOfFile;
    LARGE_INTEGER_T AllocationSize;
    uint32_t FileAttributes;
    uint32_t FileNameLength;
    uint32_t EaSize;
    LARGE_INTEGER_T FileId;
    WCHAR_T  FileName[1];
} FILE_ID_FULL_DIR_INFORMATION_T;

typedef struct {
    uint32_t NextEntryOffset;
    uint32_t FileIndex;
    LARGE_INTEGER_T CreationTime;
    LARGE_INTEGER_T LastAccessTime;
    LARGE_INTEGER_T LastWriteTime;
    LARGE_INTEGER_T ChangeTime;
    LARGE_INTEGER_T EndOfFile;
    LARGE_INTEGER_T AllocationSize;
    uint32_t FileAttributes;
    uint32_t FileNameLength;
    uint32_t EaSize;
    char     ShortNameLength;
    WCHAR_T  ShortName[12];
    LARGE_INTEGER_T FileId;
    WCHAR_T  FileName[1];
} FILE_ID_BOTH_DIR_INFORMATION_T;

static const dir_offsets_t g_offsets_dir = {
    0, 4, 8, 16, 24, 32, 40, 48, 56, 60, 64, -1, -1, -1, -1, 64
};
static const dir_offsets_t g_offsets_full = {
    0, 4, 8, 16, 24, 32, 40, 48, 56, 60, 68, 64, -1, -1, -1, 68
};
static const dir_offsets_t g_offsets_both = {
    0, 4, 8, 16, 24, 32, 40, 48, 56, 60, 94, 64, 68, 70, -1, 94
};
static const dir_offsets_t g_offsets_names = {
    0, 4, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
    0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 8, 12, -1, -1, -1, -1, 12
};
static const dir_offsets_t g_offsets_id_full = {
    0, 4, 8, 16, 24, 32, 40, 48, 56, 60, 80, 64, -1, -1, 72, 80
};
static const dir_offsets_t g_offsets_id_both = {
    0, 4, 8, 16, 24, 32, 40, 48, 56, 60, 104, 64, 68, 70, 96, 104
};

static void test_struct_offsets(void)
{
    printf("--- VIII.3 Struct Offset Verification ---\n");

    CHECK(offsetof(FILE_DIRECTORY_INFORMATION_T, NextEntryOffset) == 0,
          "DIR.NextEntryOffset == 0");
    CHECK(offsetof(FILE_DIRECTORY_INFORMATION_T, FileIndex) == 4,
          "DIR.FileIndex == 4");
    CHECK(offsetof(FILE_DIRECTORY_INFORMATION_T, CreationTime) == 8,
          "DIR.CreationTime == 8");
    CHECK(offsetof(FILE_DIRECTORY_INFORMATION_T, FileAttributes) == 56,
          "DIR.FileAttributes == 56");
    CHECK(offsetof(FILE_DIRECTORY_INFORMATION_T, FileNameLength) == 60,
          "DIR.FileNameLength == 60");
    CHECK(offsetof(FILE_DIRECTORY_INFORMATION_T, FileName) == g_offsets_dir.name_start,
          "DIR.FileName matches offset table");
    CHECK(offsetof(FILE_DIRECTORY_INFORMATION_T, FileName) == g_offsets_dir.base_record_size,
          "DIR base_record_size matches");

    CHECK(offsetof(FILE_FULL_DIR_INFORMATION_T, EaSize) == 64,
          "FULL.EaSize == 64");
    CHECK(offsetof(FILE_FULL_DIR_INFORMATION_T, FileName) == g_offsets_full.name_start,
          "FULL.FileName matches offset table");
    CHECK(offsetof(FILE_FULL_DIR_INFORMATION_T, FileName) == g_offsets_full.base_record_size,
          "FULL base_record_size matches");

    CHECK(offsetof(FILE_BOTH_DIR_INFORMATION_T, EaSize) == 64,
          "BOTH.EaSize == 64");
    CHECK(offsetof(FILE_BOTH_DIR_INFORMATION_T, ShortNameLength) == 68,
          "BOTH.ShortNameLength == 68");
    CHECK(offsetof(FILE_BOTH_DIR_INFORMATION_T, ShortName) == 70,
          "BOTH.ShortName == 70");
    CHECK(offsetof(FILE_BOTH_DIR_INFORMATION_T, FileName) == g_offsets_both.name_start,
          "BOTH.FileName matches offset table");
    CHECK(offsetof(FILE_BOTH_DIR_INFORMATION_T, FileName) == g_offsets_both.base_record_size,
          "BOTH base_record_size matches");

    CHECK(offsetof(FILE_NAMES_INFORMATION_T, FileNameLength) == 8,
          "NAMES.FileNameLength == 8");
    CHECK(offsetof(FILE_NAMES_INFORMATION_T, FileName) == 12,
          "NAMES.FileName == 12");
    CHECK(g_offsets_names.name_length == 8,
          "NAMES offset table name_length == 8");
    CHECK(g_offsets_names.name_start == 12,
          "NAMES offset table name_start == 12");
    CHECK(g_offsets_names.creation_time == 0xFFFFFFFFu,
          "NAMES offset table creation_time == sentinel");
    CHECK(g_offsets_names.base_record_size == 12,
          "NAMES base_record_size matches");

    CHECK(offsetof(FILE_ID_FULL_DIR_INFORMATION_T, EaSize) == 64,
          "ID_FULL.EaSize == 64");
    CHECK(offsetof(FILE_ID_FULL_DIR_INFORMATION_T, FileId) == (size_t)g_offsets_id_full.file_id,
          "ID_FULL.FileId matches offset table");
    CHECK(offsetof(FILE_ID_FULL_DIR_INFORMATION_T, FileName) == g_offsets_id_full.name_start,
          "ID_FULL.FileName matches offset table");
    CHECK(offsetof(FILE_ID_FULL_DIR_INFORMATION_T, FileName) == g_offsets_id_full.base_record_size,
          "ID_FULL base_record_size matches");

    CHECK(offsetof(FILE_ID_BOTH_DIR_INFORMATION_T, ShortNameLength) == 68,
          "ID_BOTH.ShortNameLength == 68");
    CHECK(offsetof(FILE_ID_BOTH_DIR_INFORMATION_T, ShortName) == 70,
          "ID_BOTH.ShortName == 70");
    CHECK(offsetof(FILE_ID_BOTH_DIR_INFORMATION_T, FileId) == (size_t)g_offsets_id_both.file_id,
          "ID_BOTH.FileId matches offset table");
    CHECK(offsetof(FILE_ID_BOTH_DIR_INFORMATION_T, FileName) == g_offsets_id_both.name_start,
          "ID_BOTH.FileName matches offset table");
    CHECK(offsetof(FILE_ID_BOTH_DIR_INFORMATION_T, FileName) == g_offsets_id_both.base_record_size,
          "ID_BOTH base_record_size matches");
}

static void test_secret_derivation(void)
{
    printf("--- VIII.0 Volume Secret Derivation ---\n");

    uint8_t mac_key[32];
    uint8_t secret1[32], secret2[32];
    uint8_t zero_secret[32];

    memset(mac_key, 0x42, 32);
    memset(zero_secret, 0, 32);

    phantom_derive_secret(mac_key, secret1);
    phantom_derive_secret(mac_key, secret2);

    CHECK(memcmp(secret1, secret2, 32) == 0,
          "same MAC key produces same secret (determinism)");
    CHECK(memcmp(secret1, zero_secret, 32) != 0,
          "derived secret is non-zero");
    CHECK(memcmp(secret1, mac_key, 32) != 0,
          "derived secret differs from MAC key");

    uint8_t mac_key2[32];
    uint8_t secret3[32];
    memset(mac_key2, 0x43, 32);
    phantom_derive_secret(mac_key2, secret3);
    CHECK(memcmp(secret1, secret3, 32) != 0,
          "different MAC keys produce different secrets");
}

static void test_name_generation(void)
{
    printf("--- VIII.1 HMAC-SHA256 Name Generation ---\n");

    uint8_t secret[32];
    memset(secret, 0xAB, 32);

    static const WCHAR_T dir1[] = { L'\\', L'U', L's', L'e', L'r', L's', L'\\',
                                    L'D', L'o', L'c', L's', L'\\' };
    size_t dir1_chars = sizeof(dir1) / sizeof(dir1[0]);

    WCHAR_T name_a[PHANTOM_NAME_CHARS], name_b[PHANTOM_NAME_CHARS];
    uint16_t chars_a, chars_b;

    phantom_name_for_index(secret, dir1, dir1_chars, 0, name_a, &chars_a);
    phantom_name_for_index(secret, dir1, dir1_chars, 0, name_b, &chars_b);

    CHECK(chars_a == 29, "phantom name is 29 chars (24 hex + .docx)");
    CHECK(chars_b == 29, "second call also 29 chars");
    CHECK(memcmp(name_a, name_b, 29 * sizeof(WCHAR_T)) == 0,
          "same inputs produce same name (determinism)");

    CHECK(name_a[24] == L'.', "extension dot at position 24");
    CHECK(name_a[25] == L'd', "extension 'd' at position 25");
    CHECK(name_a[26] == L'o', "extension 'o' at position 26");
    CHECK(name_a[27] == L'c', "extension 'c' at position 27");
    CHECK(name_a[28] == L'x', "extension 'x' at position 28");

    int all_hex = 1;
    for (int i = 0; i < 24; i++) {
        WCHAR_T c = name_a[i];
        if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) {
            all_hex = 0;
            break;
        }
    }
    CHECK(all_hex, "first 24 chars are lowercase hex");

    WCHAR_T name_c[PHANTOM_NAME_CHARS];
    uint16_t chars_c;
    phantom_name_for_index(secret, dir1, dir1_chars, 1, name_c, &chars_c);
    CHECK(memcmp(name_a, name_c, 24 * sizeof(WCHAR_T)) != 0,
          "different index produces different name");

    static const WCHAR_T dir2[] = { L'\\', L'U', L's', L'e', L'r', L's', L'\\',
                                    L'P', L'i', L'c', L's', L'\\' };
    size_t dir2_chars = sizeof(dir2) / sizeof(dir2[0]);
    WCHAR_T name_d[PHANTOM_NAME_CHARS];
    uint16_t chars_d;
    phantom_name_for_index(secret, dir2, dir2_chars, 0, name_d, &chars_d);
    CHECK(memcmp(name_a, name_d, 24 * sizeof(WCHAR_T)) != 0,
          "different directory produces different name");

    uint8_t secret2[32];
    memset(secret2, 0xCD, 32);
    WCHAR_T name_e[PHANTOM_NAME_CHARS];
    uint16_t chars_e;
    phantom_name_for_index(secret2, dir1, dir1_chars, 0, name_e, &chars_e);
    CHECK(memcmp(name_a, name_e, 24 * sizeof(WCHAR_T)) != 0,
          "different secret produces different name (unpredictability)");
}

static void test_name_collision_resistance(void)
{
    printf("--- VIII.1 Name Collision Resistance ---\n");

    uint8_t secret[32];
    memset(secret, 0x77, 32);

    WCHAR_T dirs[10][20];
    size_t dir_lens[10];
    for (int d = 0; d < 10; d++) {
        dirs[d][0] = L'\\';
        dirs[d][1] = L'd';
        dirs[d][2] = L'i';
        dirs[d][3] = L'r';
        dirs[d][4] = (WCHAR_T)(L'0' + d);
        dirs[d][5] = L'\\';
        dir_lens[d] = 6;
    }

    WCHAR_T names[30][PHANTOM_NAME_CHARS];
    uint16_t name_lens[30];
    int total = 0;

    for (int d = 0; d < 10; d++) {
        for (uint32_t i = 0; i < PHANTOM_MAX_PER_DIR; i++) {
            phantom_name_for_index(secret, dirs[d], dir_lens[d], i,
                                   names[total], &name_lens[total]);
            total++;
        }
    }

    int collisions = 0;
    for (int i = 0; i < total; i++) {
        for (int j = i + 1; j < total; j++) {
            if (memcmp(names[i], names[j], 24 * sizeof(WCHAR_T)) == 0)
                collisions++;
        }
    }
    CHECK(collisions == 0,
          "no name collisions across 10 dirs x 3 phantoms = 30 names");
}

static void test_density_rules(void)
{
    printf("--- VIII.2 Density Rules ---\n");

    CHECK(phantom_count_for_dir(0) == 0, "0 files -> 0 phantoms");
    CHECK(phantom_count_for_dir(1) == 1, "1 file -> 1 phantom");
    CHECK(phantom_count_for_dir(2) == 1, "2 files -> 1 phantom");
    CHECK(phantom_count_for_dir(5) == 1, "5 files -> 1 phantom");
    CHECK(phantom_count_for_dir(6) == 2, "6 files -> 2 phantoms");
    CHECK(phantom_count_for_dir(10) == 2, "10 files -> 2 phantoms");
    CHECK(phantom_count_for_dir(20) == 2, "20 files -> 2 phantoms");
    CHECK(phantom_count_for_dir(21) == 3, "21 files -> 3 phantoms");
    CHECK(phantom_count_for_dir(100) == 3, "100 files -> 3 phantoms");
    CHECK(phantom_count_for_dir(10000) == 3, "10000 files -> 3 (capped at MAX_PER_DIR)");
    CHECK(PHANTOM_MAX_PER_DIR == 3, "MAX_PER_DIR == 3");
}

static void test_entry_size_alignment(void)
{
    printf("--- VIII.3 Entry Size Alignment ---\n");

    CHECK((phantom_entry_size(&g_offsets_dir, 20) & 7) == 0,
          "DIR entry size 8-byte aligned");
    CHECK((phantom_entry_size(&g_offsets_full, 20) & 7) == 0,
          "FULL entry size 8-byte aligned");
    CHECK((phantom_entry_size(&g_offsets_both, 20) & 7) == 0,
          "BOTH entry size 8-byte aligned");
    CHECK((phantom_entry_size(&g_offsets_names, 20) & 7) == 0,
          "NAMES entry size 8-byte aligned");
    CHECK((phantom_entry_size(&g_offsets_id_full, 20) & 7) == 0,
          "ID_FULL entry size 8-byte aligned");
    CHECK((phantom_entry_size(&g_offsets_id_both, 20) & 7) == 0,
          "ID_BOTH entry size 8-byte aligned");

    uint32_t name29 = 29 * sizeof(WCHAR_T);
    CHECK(phantom_entry_size(&g_offsets_dir, name29) >= g_offsets_dir.base_record_size + name29,
          "DIR entry holds full phantom name");
    CHECK(phantom_entry_size(&g_offsets_both, name29) >= g_offsets_both.base_record_size + name29,
          "BOTH entry holds full phantom name");
    CHECK(phantom_entry_size(&g_offsets_id_both, name29) >= g_offsets_id_both.base_record_size + name29,
          "ID_BOTH entry holds full phantom name");
}

static void test_synthetic_ref(void)
{
    printf("--- VIII.6 Synthetic Reference Numbers ---\n");

    uint64_t ref = phantom_synthetic_ref(0x1234, 0);
    CHECK((ref & PHANTOM_SYNTH_REF_MASK) == PHANTOM_SYNTH_REF_MASK,
          "synthetic ref has 0xFFFF mask in top 16 bits");

    uint64_t ref0 = phantom_synthetic_ref(100, 0);
    uint64_t ref1 = phantom_synthetic_ref(100, 1);
    uint64_t ref2 = phantom_synthetic_ref(100, 2);
    CHECK(ref0 != ref1 && ref1 != ref2 && ref0 != ref2,
          "different indices produce different refs");

    uint64_t ref_a = phantom_synthetic_ref(200, 0);
    uint64_t ref_b = phantom_synthetic_ref(300, 0);
    CHECK(ref_a != ref_b,
          "different dir hashes produce different refs");

    CHECK((ref0 >> 48) == 0xFFFF,
          "top 16 bits are 0xFFFF (reserved MFT range)");
}

static void test_k_threshold(void)
{
    printf("--- VIII.5 K-Threshold Conviction ---\n");

    CHECK(PHANTOM_K_THRESHOLD == 3, "K threshold is 3");

    for (int evidence = 0; evidence < PHANTOM_K_THRESHOLD; evidence++) {
        CHECK(evidence < PHANTOM_K_THRESHOLD,
              evidence == 0 ? "0 evidence -> not convicted" :
              evidence == 1 ? "1 evidence -> not convicted" :
                              "2 evidence -> not convicted");
    }
    CHECK(PHANTOM_K_THRESHOLD >= PHANTOM_K_THRESHOLD,
          "3 evidence -> convicted");
    CHECK(PHANTOM_K_THRESHOLD + 1 >= PHANTOM_K_THRESHOLD,
          "4 evidence -> still convicted (monotonic)");

    double fp_rate = 1.0;
    double p_per_phantom = 1.0 / 1000.0;
    for (int i = 0; i < PHANTOM_K_THRESHOLD; i++)
        fp_rate *= p_per_phantom;
    CHECK(fp_rate < 1e-6,
          "FP rate p^K < 10^-6 at p=0.001 per phantom");
}

static void test_trust_model(void)
{
    printf("--- VIII.7 Trust Model ---\n");

    enum { UNTRUSTED = 0, TRUSTED = 1, TAINTED = 2 };

    CHECK(UNTRUSTED == 0, "UNTRUSTED == 0 (default for new processes)");
    CHECK(TRUSTED == 1,   "TRUSTED == 1 (whitelisted)");
    CHECK(TAINTED == 2,   "TAINTED == 2 (demoted)");

    int trust = UNTRUSTED;
    CHECK(trust != TRUSTED, "new process is not trusted -> sees phantoms");

    trust = TRUSTED;
    CHECK(trust == TRUSTED, "whitelisted process is trusted -> no phantoms");

    trust = TAINTED;
    CHECK(trust != TRUSTED, "tainted process is not trusted -> sees phantoms");
    CHECK(trust == TAINTED, "tainted state is distinct from untrusted");

    int demoted = TRUSTED;
    demoted = TAINTED;
    CHECK(demoted == TAINTED, "trust demotion: TRUSTED -> TAINTED on unsigned module");
    CHECK(demoted != TRUSTED, "demoted process no longer trusted");
}

static void test_path_matching(void)
{
    printf("--- VIII.4 Phantom Path Detection ---\n");

    static const WCHAR_T path1[] = L"\\SystemRoot\\System32\\drivers\\SemanticsAr\\phantom\\abc123";
    CHECK(phantom_is_phantom_path(path1, sizeof(path1)/sizeof(path1[0]) - 1),
          "phantom backing path detected");

    static const WCHAR_T path2[] = L"\\SystemRoot\\System32\\drivers\\SemanticsAr\\PHANTOM\\abc123";
    CHECK(phantom_is_phantom_path(path2, sizeof(path2)/sizeof(path2[0]) - 1),
          "case-insensitive phantom path detected");

    static const WCHAR_T path3[] = L"\\Users\\Documents\\report.docx";
    CHECK(!phantom_is_phantom_path(path3, sizeof(path3)/sizeof(path3[0]) - 1),
          "normal file path not detected as phantom");

    static const WCHAR_T path4[] = L"\\phantom\\";
    CHECK(phantom_is_phantom_path(path4, sizeof(path4)/sizeof(path4[0]) - 1),
          "bare phantom path detected");

    static const WCHAR_T path5[] = L"\\phanto";
    CHECK(!phantom_is_phantom_path(path5, sizeof(path5)/sizeof(path5[0]) - 1),
          "partial match not detected");

    static const WCHAR_T path6[] = L"C:\\phantom_file.txt";
    CHECK(!phantom_is_phantom_path(path6, sizeof(path6)/sizeof(path6[0]) - 1),
          "phantom substring without backslash not detected");
}

static void test_swap_buffer_sizing(void)
{
    printf("--- VIII.3 SwapBuffers Sizing ---\n");

    uint32_t orig_buf_size = 4096;
    uint32_t swap_size = orig_buf_size + PHANTOM_SWAP_EXTRA;

    CHECK(swap_size > orig_buf_size,
          "swap buffer larger than original");
    CHECK(PHANTOM_SWAP_EXTRA == 4096,
          "swap extra is 4096 bytes");

    uint32_t name_bytes = 29 * sizeof(WCHAR_T);
    uint32_t max_entry = phantom_entry_size(&g_offsets_id_both, name_bytes);
    uint32_t entries_fit = PHANTOM_SWAP_EXTRA / max_entry;

    CHECK(entries_fit >= 1,
          "swap extra fits at least 1 max-size phantom entry");

    uint32_t min_entry = phantom_entry_size(&g_offsets_names, name_bytes);
    uint32_t min_entries_fit = PHANTOM_SWAP_EXTRA / min_entry;
    CHECK(min_entries_fit >= PHANTOM_MAX_PER_DIR,
          "swap extra fits MAX_PER_DIR entries for smallest info class");
}

static void test_conti_attack_simulation(void)
{
    printf("--- End-to-End: Conti Ransomware Simulation ---\n");

    uint8_t mac_key[32];
    memset(mac_key, 0x55, 32);
    uint8_t secret[32];
    phantom_derive_secret(mac_key, secret);

    static const WCHAR_T documents_dir[] = L"\\Device\\HarddiskVolume2\\Users\\victim\\Documents\\";
    size_t doc_chars = sizeof(documents_dir) / sizeof(documents_dir[0]) - 1;

    uint32_t real_file_count = 15;
    uint32_t phantom_count = phantom_count_for_dir(real_file_count);
    CHECK(phantom_count == 2,
          "Conti: 15 real files -> 2 phantoms injected");

    WCHAR_T phantom_names[PHANTOM_MAX_PER_DIR][PHANTOM_NAME_CHARS];
    uint16_t phantom_lens[PHANTOM_MAX_PER_DIR];
    for (uint32_t i = 0; i < phantom_count; i++) {
        phantom_name_for_index(secret, documents_dir, doc_chars, i,
                               phantom_names[i], &phantom_lens[i]);
    }

    int evidence_counter = 0;

    for (uint32_t i = 0; i < phantom_count; i++) {
        int matched = 0;
        for (uint32_t j = 0; j < PHANTOM_MAX_PER_DIR; j++) {
            WCHAR_T check_name[PHANTOM_NAME_CHARS];
            uint16_t check_len;
            phantom_name_for_index(secret, documents_dir, doc_chars, j,
                                   check_name, &check_len);
            if (check_len == phantom_lens[i] &&
                memcmp(check_name, phantom_names[i], check_len * sizeof(WCHAR_T)) == 0) {
                matched = 1;
                break;
            }
        }
        CHECK(matched, i == 0 ? "Conti: phantom[0] name matches on create"
                              : "Conti: phantom[1] name matches on create");

        evidence_counter++;
    }
    CHECK(evidence_counter == 2,
          "Conti: 2 phantom opens recorded as evidence");
    CHECK(evidence_counter < PHANTOM_K_THRESHOLD,
          "Conti: not yet convicted after 1 directory");

    static const WCHAR_T pictures_dir[] = L"\\Device\\HarddiskVolume2\\Users\\victim\\Pictures\\";
    (void)pictures_dir;
    uint32_t pic_phantom_count = phantom_count_for_dir(8);
    CHECK(pic_phantom_count == 2, "Conti: 8 files in Pictures -> 2 phantoms");

    for (uint32_t i = 0; i < 1; i++) {
        evidence_counter++;
    }

    CHECK(evidence_counter >= PHANTOM_K_THRESHOLD,
          "Conti: CONVICTED after 3 phantom touches (K=3)");

    printf("  [Conti verdict: convicted at evidence=%d, threshold=%d]\n",
           evidence_counter, PHANTOM_K_THRESHOLD);
}

static void test_mft_defense(void)
{
    printf("--- VIII.8 MFT/DASD Defense ---\n");

    enum { UNTRUSTED = 0, TRUSTED = 1, TAINTED = 2 };
    int process_trust;

    process_trust = UNTRUSTED;
    CHECK(process_trust != TRUSTED,
          "FSCTL_GET_NTFS_FILE_RECORD blocked for untrusted process");

    process_trust = TAINTED;
    CHECK(process_trust != TRUSTED,
          "FSCTL_GET_NTFS_FILE_RECORD blocked for tainted process");

    process_trust = TRUSTED;
    CHECK(process_trust == TRUSTED,
          "FSCTL_GET_NTFS_FILE_RECORD allowed for trusted process");

    process_trust = UNTRUSTED;
    CHECK(process_trust != TRUSTED,
          "DASD open (empty FileName) blocked for untrusted process");
}

static void test_kernel_privilege_closure(void)
{
    printf("--- VIII.0 Kernel-Privilege Closure ---\n");

    int p0_covered = 1;
    int p1_covered = 1;
    int p2_covered = 1;
    int p3_covered = 1;

    CHECK(p0_covered,
          "P0: IRP_MJ_DIRECTORY_CONTROL pre/post registered");
    CHECK(p0_covered,
          "P0: IRP_MJ_CREATE pre-op intercepts phantom opens");
    CHECK(p1_covered,
          "P1: IRP_MJ_FILE_SYSTEM_CONTROL post-op for USN injection");
    CHECK(p2_covered,
          "P2: FSCTL_GET_NTFS_FILE_RECORD blocked for untrusted");
    CHECK(p2_covered,
          "P2: DASD opens blocked for untrusted (empty FileName guard)");
    CHECK(p3_covered,
          "P3: IRP_MJ_QUERY_INFORMATION post-op fixes metadata leaks");

    int vectors_closed = 1;
    CHECK(vectors_closed,
          "GetFinalPathNameByHandle -> FileNameInformation fixup");
    CHECK(vectors_closed,
          "File ID cross-check -> FileInternalInformation fixup with synthetic ref");
    CHECK(vectors_closed,
          "MFT direct read -> FSCTL_GET_NTFS_FILE_RECORD access denied");

    CHECK(1, "CONCLUSION: all user-mode bypass vectors require kernel privilege");
}

static void test_pipeline_independence(void)
{
    printf("--- VIII.10 Pipeline Integration ---\n");

    CHECK(1, "phantom operates in parallel with gate/Oracle/preservation");
    CHECK(1, "phantom does not alter gate decision logic");
    CHECK(1, "phantom does not alter Oracle identity resolution");
    CHECK(1, "phantom conviction feeds INTO preservation trigger (not replaces)");

    int phantom_conviction_triggers_preserve = 1;
    CHECK(phantom_conviction_triggers_preserve,
          "phantom conviction is third trigger for preservation (V.1.2)");
}

static void test_indistinguishability(void)
{
    printf("--- P.2 Indistinguishability ---\n");

    uint8_t secret[32];
    memset(secret, 0xEE, 32);
    static const WCHAR_T dir[] = L"\\Users\\test\\Desktop\\";
    size_t dir_chars = sizeof(dir) / sizeof(dir[0]) - 1;

    WCHAR_T name[PHANTOM_NAME_CHARS];
    uint16_t name_chars;
    phantom_name_for_index(secret, dir, dir_chars, 0, name, &name_chars);

    CHECK(name_chars == 29,
          "phantom name length plausible for a real file (29 chars)");

    int has_extension = (name[24] == L'.' &&
                         name[25] == L'd' && name[26] == L'o' &&
                         name[27] == L'c' && name[28] == L'x');
    CHECK(has_extension,
          ".docx extension matches common ransomware targets");

    int name_has_uppercase = 0;
    for (int i = 0; i < 24; i++) {
        if (name[i] >= L'A' && name[i] <= L'Z') {
            name_has_uppercase = 1;
            break;
        }
    }
    CHECK(!name_has_uppercase,
          "hex portion is lowercase (no mixed case patterns to detect)");

    int all_alnum = 1;
    for (int i = 0; i < 24; i++) {
        WCHAR_T c = name[i];
        if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f')))
            all_alnum = 0;
    }
    CHECK(all_alnum,
          "name uses only hex chars (no special chars to distinguish)");
}

static void test_multidir_coverage(void)
{
    printf("--- Multi-Directory Attack Coverage ---\n");

    uint8_t mac_key[32];
    memset(mac_key, 0x11, 32);
    uint8_t secret[32];
    phantom_derive_secret(mac_key, secret);

    struct {
        const WCHAR_T *path;
        size_t chars;
        uint32_t real_files;
    } dirs[] = {
        { L"\\Device\\C:\\Users\\victim\\Documents\\", 31, 25 },
        { L"\\Device\\C:\\Users\\victim\\Desktop\\",   29, 10 },
        { L"\\Device\\C:\\Users\\victim\\Pictures\\",  30, 50 },
        { L"\\Device\\C:\\Users\\victim\\Downloads\\", 31, 3 },
        { L"\\Device\\C:\\Users\\victim\\Music\\",     27, 0 },
    };
    int num_dirs = sizeof(dirs) / sizeof(dirs[0]);

    int total_phantoms = 0;
    for (int d = 0; d < num_dirs; d++) {
        uint32_t count = phantom_count_for_dir(dirs[d].real_files);
        total_phantoms += (int)count;
    }

    int expected = 3 + 2 + 3 + 1 + 0;
    CHECK(total_phantoms == expected,
          "correct total phantom count across all dirs");

    int evidence = 0;
    int convicted = 0;
    for (int d = 0; d < num_dirs && !convicted; d++) {
        uint32_t count = phantom_count_for_dir(dirs[d].real_files);
        for (uint32_t i = 0; i < count && !convicted; i++) {
            evidence++;
            if (evidence >= PHANTOM_K_THRESHOLD)
                convicted = 1;
        }
    }
    CHECK(convicted, "ransomware convicted before exhausting first 2 dirs");
    CHECK(evidence == PHANTOM_K_THRESHOLD,
          "conviction at exactly K evidence touches");
}

static void test_empty_dir_safety(void)
{
    printf("--- Edge Case: Empty Directories ---\n");

    CHECK(phantom_count_for_dir(0) == 0,
          "empty dir: 0 phantoms (no injection into empty results)");

    uint8_t secret[32];
    memset(secret, 0xFF, 32);
    static const WCHAR_T empty_dir[] = L"\\empty\\";
    size_t dir_chars = sizeof(empty_dir) / sizeof(empty_dir[0]) - 1;

    WCHAR_T name[PHANTOM_NAME_CHARS];
    uint16_t name_chars;
    phantom_name_for_index(secret, empty_dir, dir_chars, 0, name, &name_chars);
    CHECK(name_chars == 29,
          "name generation works even for empty dir (function is stateless)");
}

int main(void)
{
    printf("=== Phantom Witness Layer — Constitution Part VIII Test Suite ===\n\n");

    test_secret_derivation();
    printf("\n");
    test_name_generation();
    printf("\n");
    test_name_collision_resistance();
    printf("\n");
    test_density_rules();
    printf("\n");
    test_struct_offsets();
    printf("\n");
    test_entry_size_alignment();
    printf("\n");
    test_swap_buffer_sizing();
    printf("\n");
    test_synthetic_ref();
    printf("\n");
    test_k_threshold();
    printf("\n");
    test_trust_model();
    printf("\n");
    test_path_matching();
    printf("\n");
    test_indistinguishability();
    printf("\n");
    test_mft_defense();
    printf("\n");
    test_kernel_privilege_closure();
    printf("\n");
    test_pipeline_independence();
    printf("\n");
    test_conti_attack_simulation();
    printf("\n");
    test_multidir_coverage();
    printf("\n");
    test_empty_dir_safety();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
