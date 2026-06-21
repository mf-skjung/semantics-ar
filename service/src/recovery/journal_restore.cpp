#include "service_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static BOOL is_target_pid(DWORD pid, const DWORD *targetPids, DWORD pidCount) {
    for (DWORD i = 0; i < pidCount; i++) {
        if (targetPids[i] == pid)
            return TRUE;
    }
    return FALSE;
}

static int journal_integrity_ok(const semantics_ar_journal_entry_t *entry) {
    (void)entry;
    return 1;
}

static BOOL ensure_parent_directory(const WCHAR *filePath) {
    WCHAR dirPath[MAX_PATH];
    wcsncpy_s(dirPath, MAX_PATH, filePath, _TRUNCATE);
    WCHAR *lastSep = wcsrchr(dirPath, L'\\');
    if (!lastSep)
        return TRUE;
    *lastSep = L'\0';
    DWORD attrs = GetFileAttributesW(dirPath);
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
        return TRUE;
    ensure_parent_directory(dirPath);
    return CreateDirectoryW(dirPath, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static BOOL restore_shadow_to_file(const WCHAR *dosOrigPath, const WCHAR *dosShadowPath,
                                   uint64_t offset, uint64_t length, BOOL extendEof) {
    HANDLE hShadow = CreateFileW(dosShadowPath, GENERIC_READ, FILE_SHARE_READ,
                                 NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hShadow == INVALID_HANDLE_VALUE)
        return FALSE;

    semantics_ar_shadow_overwrite_header_t header;
    DWORD bytesRead;
    if (!ReadFile(hShadow, &header, sizeof(header), &bytesRead, NULL) ||
        bytesRead != sizeof(header) ||
        header.magic != SEMANTICS_AR_SHADOW_MAGIC) {
        CloseHandle(hShadow);
        return FALSE;
    }

    HANDLE hOrig = CreateFileW(dosOrigPath, GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hOrig == INVALID_HANDLE_VALUE) {
        CloseHandle(hShadow);
        return FALSE;
    }

    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)offset;
    SetFilePointerEx(hOrig, li, NULL, FILE_BEGIN);

    uint8_t buf[65536];
    uint64_t remaining = length;
    BOOL ok = TRUE;

    while (remaining > 0) {
        DWORD chunkSize = (remaining > sizeof(buf)) ? (DWORD)sizeof(buf) : (DWORD)remaining;
        DWORD bytesReadShadow;
        if (!ReadFile(hShadow, buf, chunkSize, &bytesReadShadow, NULL) || bytesReadShadow == 0) {
            ok = FALSE;
            break;
        }
        DWORD bytesWritten;
        if (!WriteFile(hOrig, buf, bytesReadShadow, &bytesWritten, NULL)) {
            ok = FALSE;
            break;
        }
        remaining -= bytesReadShadow;
    }

    if (ok && extendEof) {
        li.QuadPart = (LONGLONG)(offset + length);
        SetFilePointerEx(hOrig, li, NULL, FILE_BEGIN);
        SetEndOfFile(hOrig);
    }

    CloseHandle(hOrig);
    CloseHandle(hShadow);
    return ok;
}

static BOOL restore_delete(const WCHAR *dosOrigPath, const WCHAR *dosShadowPath) {
    ensure_parent_directory(dosOrigPath);
    if (!MoveFileExW(dosShadowPath, dosOrigPath, MOVEFILE_REPLACE_EXISTING))
        return FALSE;
    SetFileAttributesW(dosOrigPath, FILE_ATTRIBUTE_NORMAL);
    return TRUE;
}

int svc_journal_restore(const WCHAR *volumeDosPrefix, const DWORD *targetPids, DWORD pidCount) {
    if (!volumeDosPrefix || !targetPids || pidCount == 0)
        return -1;

    WCHAR journalPath[MAX_PATH];
    _snwprintf_s(journalPath, MAX_PATH, _TRUNCATE,
                 L"%s\\" SEMANTICS_AR_SVC_SHADOW_DIR_NAME L"\\journal.dat",
                 volumeDosPrefix);

    HANDLE hJournal = CreateFileW(journalPath, GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hJournal == INVALID_HANDLE_VALUE)
        return -1;

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hJournal, &fileSize) || fileSize.QuadPart == 0) {
        CloseHandle(hJournal);
        return 0;
    }

    DWORD entrySize = (DWORD)sizeof(semantics_ar_journal_entry_t);
    LONGLONG validSize = (fileSize.QuadPart / entrySize) * entrySize;
    if (validSize == 0 || validSize > (LONGLONG)512 * 1024 * 1024) {
        CloseHandle(hJournal);
        return (validSize == 0) ? 0 : -1;
    }

    DWORD totalSize = (DWORD)validSize;
    uint8_t *buffer = (uint8_t *)malloc(totalSize);
    if (!buffer) {
        CloseHandle(hJournal);
        return -1;
    }

    DWORD bytesRead;
    if (!ReadFile(hJournal, buffer, totalSize, &bytesRead, NULL) || bytesRead != totalSize) {
        free(buffer);
        CloseHandle(hJournal);
        return -1;
    }
    CloseHandle(hJournal);

    DWORD entryCount = totalSize / entrySize;
    int restoredCount = 0;

    for (LONG i = (LONG)entryCount - 1; i >= 0; i--) {
        semantics_ar_journal_entry_t *entry =
            (semantics_ar_journal_entry_t *)(buffer + (size_t)i * entrySize);

        if (!journal_integrity_ok(entry))
            continue;
        if (!is_target_pid(entry->process_id, targetPids, pidCount))
            continue;

        WCHAR dosOrigPath[MAX_PATH];
        WCHAR dosShadowPath[MAX_PATH];
        if (!convert_nt_to_dos_path((const WCHAR *)entry->original_file_path,
                                    dosOrigPath, MAX_PATH))
            continue;
        if (!convert_nt_to_dos_path((const WCHAR *)entry->shadow_file_path,
                                    dosShadowPath, MAX_PATH))
            continue;

        BOOL ok = FALSE;
        switch (entry->entry_type) {
        case SEMANTICS_AR_JOURNAL_OVERWRITE:
            ok = restore_shadow_to_file(dosOrigPath, dosShadowPath,
                                        entry->original_offset,
                                        entry->original_length, FALSE);
            break;
        case SEMANTICS_AR_JOURNAL_DELETE:
            ok = restore_delete(dosOrigPath, dosShadowPath);
            break;
        case SEMANTICS_AR_JOURNAL_TRUNCATE:
            ok = restore_shadow_to_file(dosOrigPath, dosShadowPath,
                                        entry->original_offset,
                                        entry->original_length, TRUE);
            break;
        }
        if (ok)
            restoredCount++;
    }

    free(buffer);
    return restoredCount;
}