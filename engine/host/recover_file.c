#include "sar_recover_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

static int read_whole_file(const char *path, uint8_t **out_buf, uint64_t *out_size,
                           mode_t *out_mode) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return -1; }
    *out_mode = st.st_mode;
    uint64_t size = (uint64_t)st.st_size;
    uint8_t *buf = NULL;
    if (size > 0) {
        buf = (uint8_t *)malloc(size);
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

static int write_atomic(const char *path, const uint8_t *data, uint64_t size,
                        mode_t mode) {
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
    if (fchmod(fd, mode & 0777) != 0 || fsync(fd) != 0 || close(fd) != 0) {
        unlink(tmp); free(tmp); return -1;
    }
    if (rename(tmp, path) != 0) {
        unlink(tmp); free(tmp); return -1;
    }
    free(tmp);
    sync_parent_dir(path);
    return 0;
}

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
                                      const sar_recovery_sample_t *confirm_sample,
                                      sar_recover_file_result_t *out) {
    sar_recover_file_result_t res;
    res.status = SAR_RECOVER_INVALID;
    res.encrypted_bytes = 0;
    res.file_size = 0;

    if (!path || !rk) {
        if (out) *out = res;
        return SAR_RECOVER_INVALID;
    }

    if (confirm_sample && !sar_recover_confirm(rk, confirm_sample)) {
        res.status = SAR_RECOVER_DECLINED_MISMATCH;
        if (out) *out = res;
        return res.status;
    }

    uint8_t *ct = NULL;
    uint64_t size = 0;
    mode_t mode = 0644;
    if (read_whole_file(path, &ct, &size, &mode) != 0) {
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

    uint8_t *pt = (uint8_t *)malloc(size);
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

    if (write_atomic(path, pt, size, mode) != 0) {
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
