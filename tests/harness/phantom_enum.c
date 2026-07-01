#include <windows.h>
#include <winternl.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SYNTH_MASK 0xFFFF000000000000ULL
#define MAX_PH     8
#define NBUF       (256 * 1024)

typedef NTSTATUS (NTAPI *PFN_NtQueryDirectoryFileEx)(
    HANDLE, HANDLE, PVOID, PVOID, PIO_STATUS_BLOCK, PVOID, ULONG, ULONG, ULONG, PUNICODE_STRING);

#ifndef STATUS_NO_MORE_FILES
#define STATUS_NO_MORE_FILES ((NTSTATUS)0x80000006L)
#endif
#define SL_RESTART_SCAN 0x01

typedef struct {
    ULONG cls;
    const char *label;
    int nameLenOff;
    int nameOff;
    int fileIdOff;
} CLASSDEF;

static const CLASSDEF g_classes[] = {
    {  1, "Directory",   60,  64, -1 },
    {  2, "Full",        60,  68, -1 },
    {  3, "Both",        60,  94, -1 },
    { 12, "Names",        8,  12, -1 },
    { 38, "IdFull",      60,  80, 72 },
    { 37, "IdBoth",      60, 104, 96 },
    { 60, "IdExtd",      60,  88, 72 },
    { 63, "IdExtdBoth",  60, 114, 72 },
    { 50, "IdGlobalTx",  60,  92, 64 },
};

typedef struct {
    WCHAR name[64];
    DWORDLONG frn;
    int hasFrn;
} PHREC;

static int is_phantom_frn(DWORDLONG frn) {
    return (frn & SYNTH_MASK) == SYNTH_MASK;
}

static int name_eq(const WCHAR *a, const WCHAR *b) {
    return _wcsicmp(a, b) == 0;
}

static PFN_NtQueryDirectoryFileEx g_query;

static int enum_class(HANDLE dir, const CLASSDEF *cd, PHREC *out, int cap, int wantPhantomOnly) {
    static UCHAR buf[NBUF];
    IO_STATUS_BLOCK iosb;
    ULONG flags = SL_RESTART_SCAN;
    int n = 0;
    for (;;) {
        NTSTATUS st = g_query(dir, NULL, NULL, NULL, &iosb, buf, NBUF, cd->cls, flags, NULL);
        flags = 0;
        if (st == STATUS_NO_MORE_FILES) break;
        if (st < 0) break;
        UCHAR *cur = buf;
        for (;;) {
            ULONG next = *(ULONG *)(cur + 0);
            ULONG nameLen = *(ULONG *)(cur + cd->nameLenOff);
            WCHAR *nm = (WCHAR *)(cur + cd->nameOff);
            DWORDLONG frn = 0;
            int hasFrn = 0;
            if (cd->fileIdOff >= 0) { frn = *(DWORDLONG *)(cur + cd->fileIdOff); hasFrn = 1; }
            ULONG nchars = nameLen / sizeof(WCHAR);
            if (nchars > 0 && nchars < 63) {
                int keep = 1;
                if (wantPhantomOnly && !(hasFrn && is_phantom_frn(frn))) keep = 0;
                if (keep && n < cap) {
                    memcpy(out[n].name, nm, nameLen);
                    out[n].name[nchars] = 0;
                    out[n].frn = frn;
                    out[n].hasFrn = hasFrn;
                    n++;
                }
            }
            if (next == 0) break;
            cur += next;
        }
    }
    return n;
}

static int find_by_name(PHREC *set, int n, const WCHAR *name) {
    for (int i = 0; i < n; i++)
        if (name_eq(set[i].name, name)) return i;
    return -1;
}

static void volume_path(WCHAR letter, WCHAR *out) {
    out[0] = L'\\'; out[1] = L'\\'; out[2] = L'.'; out[3] = L'\\';
    out[4] = letter; out[5] = L':'; out[6] = 0;
}

static int enum_usn(WCHAR letter, DWORDLONG parentFrn, PHREC *out, int cap) {
    WCHAR vol[16];
    volume_path(letter, vol);
    HANDLE v = CreateFileW(vol, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, 0, NULL);
    if (v == INVALID_HANDLE_VALUE) return -1;

    MFT_ENUM_DATA_V0 med;
    med.StartFileReferenceNumber = 0;
    med.LowUsn = 0;
    med.HighUsn = MAXLONGLONG;
    static UCHAR buf[NBUF];
    int n = 0;
    for (;;) {
        DWORD ret = 0;
        if (!DeviceIoControl(v, FSCTL_ENUM_USN_DATA, &med, sizeof(med), buf, NBUF, &ret, NULL))
            break;
        if (ret <= sizeof(DWORDLONG)) break;
        DWORDLONG nextStart = *(DWORDLONG *)buf;
        UCHAR *cur = buf + sizeof(DWORDLONG);
        UCHAR *end = buf + ret;
        while (cur + sizeof(USN_RECORD_V2) <= end) {
            USN_RECORD_V2 *r = (USN_RECORD_V2 *)cur;
            if (r->RecordLength == 0) break;
            if (r->MajorVersion == 2) {
                if (r->ParentFileReferenceNumber == parentFrn) {
                    ULONG nchars = r->FileNameLength / sizeof(WCHAR);
                    WCHAR *nm = (WCHAR *)(cur + r->FileNameOffset);
                    if (nchars > 0 && nchars < 63 && n < cap) {
                        memcpy(out[n].name, nm, r->FileNameLength);
                        out[n].name[nchars] = 0;
                        out[n].frn = r->FileReferenceNumber;
                        out[n].hasFrn = 1;
                        n++;
                    }
                }
            }
            cur += r->RecordLength;
        }
        med.StartFileReferenceNumber = nextStart;
    }
    CloseHandle(v);
    return n;
}

static DWORDLONG dir_frn(HANDLE dir) {
    FILE_ID_INFO fid;
    if (GetFileInformationByHandleEx(dir, FileIdInfo, &fid, sizeof(fid))) {
        DWORDLONG frn = 0;
        memcpy(&frn, &fid.FileId, sizeof(frn));
        return frn;
    }
    BY_HANDLE_FILE_INFORMATION bhi;
    if (GetFileInformationByHandle(dir, &bhi))
        return ((DWORDLONG)bhi.nFileIndexHigh << 32) | bhi.nFileIndexLow;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: phantom_enum <dir>\n");
        return 2;
    }
    char *dirPath = argv[1];
    WCHAR wdir[512];
    MultiByteToWideChar(CP_ACP, 0, dirPath, -1, wdir, 512);

    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    g_query = (PFN_NtQueryDirectoryFileEx)GetProcAddress(nt, "NtQueryDirectoryFileEx");
    if (g_query == NULL) { printf("RESULT=ERROR no NtQueryDirectoryFileEx\n"); return 3; }

    HANDLE dir = CreateFileW(wdir, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (dir == INVALID_HANDLE_VALUE) { printf("RESULT=ERROR open dir %lu\n", GetLastError()); return 3; }

    DWORDLONG parentFrn = dir_frn(dir);
    WCHAR driveLetter = (wcslen(wdir) >= 2 && wdir[1] == L':') ? wdir[0] : L'C';

    PHREC ref[MAX_PH];
    int refN = enum_class(dir, &g_classes[5], ref, MAX_PH, 1);
    printf("PARENT_FRN=%016llx\n", parentFrn);
    printf("PHANTOM_REF_COUNT=%d\n", refN);
    for (int i = 0; i < refN; i++)
        printf("PHANTOM name=%ls frn=%016llx\n", ref[i].name, ref[i].frn);

    int tell = 0;

    for (size_t c = 0; c < sizeof(g_classes) / sizeof(g_classes[0]); c++) {
        PHREC all[512];
        int an = enum_class(dir, &g_classes[c], all, 512, 0);
        int matched = 0, frnConsistent = 0, frnChecked = 0;
        for (int i = 0; i < refN; i++) {
            int idx = find_by_name(all, an, ref[i].name);
            if (idx >= 0) {
                matched++;
                if (all[idx].hasFrn) {
                    frnChecked++;
                    if (all[idx].frn == ref[i].frn) frnConsistent++;
                    else tell++;
                }
            } else {
                tell++;
            }
        }
        printf("METHOD=%-11s entries=%d phantom_matched=%d/%d frn_consistent=%d/%d\n",
               g_classes[c].label, an, matched, refN, frnConsistent, frnChecked);
    }

    PHREC usn[512];
    int un = enum_usn(driveLetter, parentFrn, usn, 512);
    if (un < 0) {
        printf("METHOD=USN entries=ERR (need admin/volume handle)\n");
    } else {
        int matched = 0, frnConsistent = 0;
        for (int i = 0; i < refN; i++) {
            int idx = find_by_name(usn, un, ref[i].name);
            if (idx >= 0) {
                matched++;
                if (usn[idx].frn == ref[i].frn) frnConsistent++;
                else tell++;
            } else {
                tell++;
            }
        }
        printf("METHOD=USN         entries=%d phantom_matched=%d/%d frn_consistent=%d/%d\n",
               un, matched, refN, frnConsistent, refN);
    }

    for (int i = 0; i < refN; i++) {
        FILE_ID_DESCRIPTOR fd;
        fd.dwSize = sizeof(fd);
        fd.Type = FileIdType;
        fd.FileId.QuadPart = (LONGLONG)ref[i].frn;
        HANDLE oh = OpenFileById(dir, &fd, GENERIC_READ, FILE_SHARE_READ, NULL, 0);
        if (oh != INVALID_HANDLE_VALUE) {
            UCHAR probe[16] = {0};
            DWORD got = 0;
            ReadFile(oh, probe, sizeof(probe), &got, NULL);
            printf("OPENBYID name=%ls frn=%016llx result=OK bytes=%lu\n", ref[i].name, ref[i].frn, got);
            CloseHandle(oh);
        } else {
            printf("OPENBYID name=%ls frn=%016llx result=FAIL err=%lu\n",
                   ref[i].name, ref[i].frn, GetLastError());
            tell++;
        }
    }

    printf("CONSISTENT=%s tells=%d\n", tell == 0 ? "YES" : "NO", tell);
    CloseHandle(dir);
    return tell == 0 ? 0 : 1;
}
