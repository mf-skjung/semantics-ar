#include "sar_recover_file.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)

#include <windows.h>

static wchar_t *sar_widen(const char *path) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (wlen <= 0)
        return NULL;
    wchar_t *w = (wchar_t *)malloc((size_t)wlen * sizeof(wchar_t));
    if (!w)
        return NULL;
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, w, wlen) <= 0) {
        free(w);
        return NULL;
    }
    return w;
}

static int sar_read_file(const char *path, uint8_t **out_buf, uint64_t *out_size) {
    wchar_t *wpath = sar_widen(path);
    if (!wpath)
        return -1;

    HANDLE h = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wpath);
    if (h == INVALID_HANDLE_VALUE)
        return -1;

    LARGE_INTEGER li;
    if (!GetFileSizeEx(h, &li)) {
        CloseHandle(h);
        return -1;
    }

    uint64_t size = (uint64_t)li.QuadPart;
    uint8_t *buf = NULL;
    if (size > 0) {
        buf = (uint8_t *)malloc((size_t)size);
        if (!buf) {
            CloseHandle(h);
            return -1;
        }
        uint64_t got = 0;
        while (got < size) {
            uint64_t remain = size - got;
            DWORD want = remain > 0x40000000ULL ? 0x40000000UL : (DWORD)remain;
            DWORD read = 0;
            if (!ReadFile(h, buf + got, want, &read, NULL) || read == 0) {
                free(buf);
                CloseHandle(h);
                return -1;
            }
            got += read;
        }
    }

    CloseHandle(h);
    *out_buf = buf;
    *out_size = size;
    return 0;
}

static int sar_rename_posix(const wchar_t *temp, const wchar_t *target) {
    HANDLE h = CreateFileW(temp, DELETE | SYNCHRONIZE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return -1;

    size_t name_bytes = wcslen(target) * sizeof(wchar_t);
    size_t info_bytes = sizeof(FILE_RENAME_INFO) + name_bytes;
    FILE_RENAME_INFO *info = (FILE_RENAME_INFO *)malloc(info_bytes);
    if (!info) {
        CloseHandle(h);
        return -1;
    }

    memset(info, 0, info_bytes);
    info->Flags = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS;
    info->RootDirectory = NULL;
    info->FileNameLength = (DWORD)name_bytes;
    memcpy(info->FileName, target, name_bytes);

    BOOL ok = SetFileInformationByHandle(h, FileRenameInfoEx, info, (DWORD)info_bytes);
    free(info);
    CloseHandle(h);
    return ok ? 0 : -1;
}

static int sar_replace_w(const wchar_t *wtarget, const wchar_t *wrepl) {
    if (ReplaceFileW(wtarget, wrepl, NULL,
                     REPLACEFILE_IGNORE_MERGE_ERRORS | REPLACEFILE_IGNORE_ACL_ERRORS,
                     NULL, NULL))
        return 0;
    if (GetLastError() == ERROR_UNABLE_TO_MOVE_REPLACEMENT)
        return 0;
    return sar_rename_posix(wrepl, wtarget);
}

int sar_atomic_replace_file(const char *target_path, const char *replacement_path) {
    wchar_t *wtarget = sar_widen(target_path);
    wchar_t *wrepl = sar_widen(replacement_path);
    int result = -1;
    if (wtarget && wrepl) {
        result = sar_replace_w(wtarget, wrepl);
        if (result != 0)
            DeleteFileW(wrepl);
    }
    free(wtarget);
    free(wrepl);
    return result;
}

static int sar_write_replace(const char *path, const uint8_t *data, uint64_t size) {
    wchar_t *wpath = sar_widen(path);
    if (!wpath)
        return -1;

    size_t tlen = wcslen(wpath) + 16;
    wchar_t *wtemp = (wchar_t *)malloc(tlen * sizeof(wchar_t));
    if (!wtemp) {
        free(wpath);
        return -1;
    }
    wcscpy_s(wtemp, tlen, wpath);
    wcscat_s(wtemp, tlen, L".sarrectmp");

    HANDLE h = CreateFileW(wtemp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        free(wpath);
        free(wtemp);
        return -1;
    }

    uint64_t put = 0;
    while (put < size) {
        uint64_t remain = size - put;
        DWORD want = remain > 0x40000000ULL ? 0x40000000UL : (DWORD)remain;
        DWORD wrote = 0;
        if (!WriteFile(h, data + put, want, &wrote, NULL) || wrote == 0) {
            CloseHandle(h);
            DeleteFileW(wtemp);
            free(wpath);
            free(wtemp);
            return -1;
        }
        put += wrote;
    }

    if (!FlushFileBuffers(h)) {
        CloseHandle(h);
        DeleteFileW(wtemp);
        free(wpath);
        free(wtemp);
        return -1;
    }
    CloseHandle(h);

    int result = sar_replace_w(wpath, wtemp);
    if (result != 0)
        DeleteFileW(wtemp);

    free(wpath);
    free(wtemp);
    return result;
}

#else

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

static int sar_read_file(const char *path, uint8_t **out_buf, uint64_t *out_size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return -1; }
    uint64_t size = (uint64_t)st.st_size;
    uint8_t *buf = NULL;
    if (size > 0) {
        buf = (uint8_t *)malloc((size_t)size);
        if (!buf) { close(fd); return -1; }
        uint64_t got = 0;
        while (got < size) {
            ssize_t n = read(fd, buf + got, (size_t)(size - got));
            if (n <= 0) { free(buf); close(fd); return -1; }
            got += (uint64_t)n;
        }
    }
    close(fd);
    *out_buf = buf;
    *out_size = size;
    return 0;
}

static int sync_parent_dir(const char *path) {
    char *copy = strdup(path);
    if (!copy) return -1;
    char *slash = strrchr(copy, '/');
    const char *dir = ".";
    if (slash) { *slash = '\0'; dir = copy[0] ? copy : "/"; }
    int fd = open(dir, O_RDONLY);
    free(copy);
    if (fd < 0) return -1;
    int r = fsync(fd);
    close(fd);
    return r;
}

int sar_atomic_replace_file(const char *target_path, const char *replacement_path) {
    if (rename(replacement_path, target_path) != 0) {
        unlink(replacement_path);
        return -1;
    }
    sync_parent_dir(target_path);
    return 0;
}

static int sar_write_replace(const char *path, const uint8_t *data, uint64_t size) {
    mode_t mode = 0644;
    struct stat st;
    if (stat(path, &st) == 0) mode = st.st_mode & 0777;

    size_t tlen = strlen(path) + 16;
    char *tmp = (char *)malloc(tlen);
    if (!tmp) return -1;
    snprintf(tmp, tlen, "%s.sarrecXXXXXX", path);

    int fd = mkstemp(tmp);
    if (fd < 0) { free(tmp); return -1; }

    uint64_t put = 0;
    while (put < size) {
        ssize_t n = write(fd, data + put, (size_t)(size - put));
        if (n <= 0) { close(fd); unlink(tmp); free(tmp); return -1; }
        put += (uint64_t)n;
    }
    if (fchmod(fd, mode) != 0 || fsync(fd) != 0 || close(fd) != 0) {
        unlink(tmp); free(tmp); return -1;
    }
    if (rename(tmp, path) != 0) {
        unlink(tmp); free(tmp); return -1;
    }
    free(tmp);
    sync_parent_dir(path);
    return 0;
}

#endif

static uint64_t count_encrypted(const sar_geometry_t *geom, uint64_t file_size) {
    if (!geom) return file_size;
    sar_range_t *ranges = (sar_range_t *)malloc(sizeof(sar_range_t) * SAR_MAX_RANGES);
    if (!ranges) return 0;
    uint32_t nr = 0;
    uint64_t total = 0;
    if (sar_geometry_expand(geom, file_size, ranges, SAR_MAX_RANGES, &nr) == 0) {
        for (uint32_t i = 0; i < nr; i++) total += ranges[i].length;
    }
    free(ranges);
    return total;
}

sar_recover_status_t sar_recover_file(const char *path,
                                      const sar_recovery_key_t *rk,
                                      const sar_geometry_t *geom,
                                      const sar_recovery_verify_t *verify,
                                      sar_recover_file_result_t *out) {
    sar_recover_file_result_t res;
    res.status = SAR_RECOVER_INVALID;
    res.encrypted_bytes = 0;
    res.file_size = 0;

    if (!path || !rk) {
        if (out) *out = res;
        return SAR_RECOVER_INVALID;
    }

    uint8_t *ct = NULL;
    uint64_t size = 0;
    if (sar_read_file(path, &ct, &size) != 0) {
        res.status = SAR_RECOVER_INVALID;
        if (out) *out = res;
        return res.status;
    }
    res.file_size = size;

    if (size == 0) {
        free(ct);
        res.status = SAR_RECOVER_OK;
        if (out) *out = res;
        return res.status;
    }

    uint8_t *pt = (uint8_t *)malloc((size_t)size);
    if (!pt) {
        free(ct);
        res.status = SAR_RECOVER_INVALID;
        if (out) *out = res;
        return res.status;
    }

    sar_recover_status_t st = sar_recover_buffer(rk, geom, ct, pt, size);
    if (st != SAR_RECOVER_OK) {
        free(ct); free(pt);
        res.status = st;
        if (out) *out = res;
        return res.status;
    }

    if (verify) {
        sar_recover_status_t vst = sar_recover_verify(pt, size, verify);
        if (vst != SAR_RECOVER_OK) {
            free(ct); free(pt);
            res.status = vst;
            if (out) *out = res;
            return res.status;
        }
    }

    if (sar_write_replace(path, pt, size) != 0) {
        free(ct); free(pt);
        res.status = SAR_RECOVER_INVALID;
        if (out) *out = res;
        return res.status;
    }

    res.encrypted_bytes = count_encrypted(geom, size);
    res.status = SAR_RECOVER_OK;
    free(ct); free(pt);
    if (out) *out = res;
    return res.status;
}
