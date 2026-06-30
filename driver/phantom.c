#include "phantom.h"
#include "state.h"
#include "seam.h"
#include "sha256.h"
#include "eng_mem.h"
#include "store_io.h"
#include "capture.h"

extern SAR_GLOBALS g_sar;
extern PSAR_STATE g_sar_state;

typedef NTSTATUS (*PIO_REPLACE_FILE_OBJECT_NAME_FN)(
    _In_ PFILE_OBJECT FileObject,
    _In_reads_bytes_(FileNameLength) PWSTR NewFileName,
    _In_ USHORT FileNameLength);

static PIO_REPLACE_FILE_OBJECT_NAME_FN g_sar_io_replace_name;

#define SAR_PM_IDMAP_SLOTS 256
typedef struct _SAR_PM_IDENT {
    BOOLEAN valid;
    USHORT  backlen;
    USHORT  dirlen;
    USHORT  namelen;
    ULONG   index;
    WCHAR   backhex[48];
    WCHAR   dir[284];
    WCHAR   name[SAR_PHANTOM_NAME_CHARS];
} SAR_PM_IDENT;
static SAR_PM_IDENT g_pm_idmap[SAR_PM_IDMAP_SLOTS];
static ULONG g_pm_idmap_next;
static EX_PUSH_LOCK g_pm_idmap_lock;

static VOID SarPmIdMapInsert(_In_ PCUNICODE_STRING BackHex, _In_ PCUNICODE_STRING Dir,
                             _In_ PCUNICODE_STRING Name, _In_ ULONG Index)
{
    SAR_PM_IDENT *e;
    if (BackHex->Length > sizeof(((SAR_PM_IDENT*)0)->backhex) ||
        Dir->Length > sizeof(((SAR_PM_IDENT*)0)->dir) ||
        Name->Length > sizeof(((SAR_PM_IDENT*)0)->name))
        return;
    FltAcquirePushLockExclusive(&g_pm_idmap_lock);
    e = &g_pm_idmap[g_pm_idmap_next % SAR_PM_IDMAP_SLOTS];
    g_pm_idmap_next++;
    e->valid = TRUE;
    e->backlen = BackHex->Length;
    e->dirlen = Dir->Length;
    e->namelen = Name->Length;
    e->index = Index;
    RtlCopyMemory(e->backhex, BackHex->Buffer, BackHex->Length);
    RtlCopyMemory(e->dir, Dir->Buffer, Dir->Length);
    RtlCopyMemory(e->name, Name->Buffer, Name->Length);
    FltReleasePushLock(&g_pm_idmap_lock);
}

static BOOLEAN SarPmIdMapLookup(_In_ PCUNICODE_STRING BackHex,
                                _Out_writes_bytes_(568) WCHAR *DirOut, _Out_ PUSHORT DirLen,
                                _Out_writes_bytes_(SAR_PHANTOM_NAME_CHARS * 2) WCHAR *NameOut, _Out_ PUSHORT NameLen,
                                _Out_ PULONG Index)
{
    ULONG i;
    BOOLEAN found = FALSE;
    FltAcquirePushLockShared(&g_pm_idmap_lock);
    for (i = 0; i < SAR_PM_IDMAP_SLOTS; i++) {
        SAR_PM_IDENT *e = &g_pm_idmap[i];
        if (!e->valid || e->backlen != BackHex->Length)
            continue;
        if (RtlEqualMemory(e->backhex, BackHex->Buffer, BackHex->Length)) {
            RtlCopyMemory(DirOut, e->dir, e->dirlen);
            *DirLen = e->dirlen;
            RtlCopyMemory(NameOut, e->name, e->namelen);
            *NameLen = e->namelen;
            *Index = e->index;
            found = TRUE;
            break;
        }
    }
    FltReleasePushLock(&g_pm_idmap_lock);
    return found;
}

static const SAR_DIR_OFFSETS g_dir_offsets_dir = {
    0, 4, 8, 16, 24, 32, 40, 48, 56, 60, 64, -1, -1, -1, -1, -1, 64
};
static const SAR_DIR_OFFSETS g_dir_offsets_full = {
    0, 4, 8, 16, 24, 32, 40, 48, 56, 60, 68, 64, -1, -1, -1, -1, 68
};
static const SAR_DIR_OFFSETS g_dir_offsets_both = {
    0, 4, 8, 16, 24, 32, 40, 48, 56, 60, 94, 64, 68, 70, -1, -1, 94
};
static const SAR_DIR_OFFSETS g_dir_offsets_names = {
    0, 4, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 8, 12, -1, -1, -1, -1, -1, 12
};
static const SAR_DIR_OFFSETS g_dir_offsets_id_full = {
    0, 4, 8, 16, 24, 32, 40, 48, 56, 60, 80, 64, -1, -1, 72, -1, 80
};
static const SAR_DIR_OFFSETS g_dir_offsets_id_both = {
    0, 4, 8, 16, 24, 32, 40, 48, 56, 60, 104, 64, 68, 70, 96, -1, 104
};
static const SAR_DIR_OFFSETS g_dir_offsets_id_extd = {
    0, 4, 8, 16, 24, 32, 40, 48, 56, 60, 88, 64, -1, -1, -1, 72, 88
};
static const SAR_DIR_OFFSETS g_dir_offsets_id_extd_both = {
    0, 4, 8, 16, 24, 32, 40, 48, 56, 60, 114, 64, 88, 90, -1, 72, 114
};
static const SAR_DIR_OFFSETS g_dir_offsets_id_global_tx = {
    0, 4, 8, 16, 24, 32, 40, 48, 56, 60, 92, -1, -1, -1, 64, -1, 92
};

static const SAR_DIR_OFFSETS *SarPhantomDirOffsets(FILE_INFORMATION_CLASS cls)
{
    switch (cls) {
    case FileDirectoryInformation:       return &g_dir_offsets_dir;
    case FileFullDirectoryInformation:   return &g_dir_offsets_full;
    case FileBothDirectoryInformation:   return &g_dir_offsets_both;
    case FileNamesInformation:           return &g_dir_offsets_names;
    case FileIdFullDirectoryInformation: return &g_dir_offsets_id_full;
    case FileIdBothDirectoryInformation: return &g_dir_offsets_id_both;
    case FileIdExtdDirectoryInformation: return &g_dir_offsets_id_extd;
    case FileIdExtdBothDirectoryInformation: return &g_dir_offsets_id_extd_both;
    case FileIdGlobalTxDirectoryInformation: return &g_dir_offsets_id_global_tx;
    default: return NULL;
    }
}

static ULONG SarPhantomEntrySize(const SAR_DIR_OFFSETS *off, ULONG name_bytes)
{
    ULONG size = off->base_record_size + name_bytes;
    return (size + 7) & ~7u;
}

static VOID SarPhantomDeriveSecret(_In_ const UCHAR MacKey[32],
                                   _Out_writes_(32) UCHAR Secret[32])
{
    static const UINT8 label[] = "phantom-volume-secret";
    sar_hmac_sha256(MacKey, 32, label, sizeof(label) - 1, Secret);
}

static VOID SarPhantomCanonDir(_In_ PCUNICODE_STRING In,
                               _Outptr_result_buffer_(*OutBytes) const WCHAR **OutBuf,
                               _Out_ PULONG OutBytes)
{
    static const WCHAR dev[] = L"\\Device\\";
    const ULONG dev_chars = RTL_NUMBER_OF(dev) - 1;
    const WCHAR *b = In->Buffer;
    ULONG chars = In->Length / sizeof(WCHAR);
    ULONG i, seen;
    BOOLEAN is_dev = (chars >= dev_chars);

    for (i = 0; is_dev && i < dev_chars; i++) {
        WCHAR c = b[i], d = dev[i];
        if (c >= L'A' && c <= L'Z') c = (WCHAR)(c - L'A' + L'a');
        if (d >= L'A' && d <= L'Z') d = (WCHAR)(d - L'A' + L'a');
        if (c != d) is_dev = FALSE;
    }
    if (is_dev) {
        for (i = 0, seen = 0; i < chars; i++) {
            if (b[i] == L'\\' && ++seen == 3)
                break;
        }
        if (seen == 3) { b += i; chars -= i; }
    }
    if (chars > 0 && b[chars - 1] == L'\\')
        chars--;
    *OutBuf = b;
    *OutBytes = chars * sizeof(WCHAR);
}

static const WCHAR g_phantom_cons[] = L"bcdfghjklmnprstvwz";
static const WCHAR g_phantom_vow[]  = L"aeiou";
static const WCHAR * const g_phantom_ext[] = {
    L"docx", L"doc", L"xlsx", L"xls", L"pptx", L"ppt", L"pdf", L"rtf",
    L"txt", L"csv", L"odt", L"ods", L"jpg", L"jpeg", L"png", L"gif",
    L"bmp", L"tiff", L"psd", L"zip", L"rar", L"7z", L"tar", L"gz",
    L"mp3", L"mp4", L"avi", L"mov", L"wav", L"sql", L"mdb", L"accdb",
    L"db", L"dwg", L"dxf", L"ai", L"eps", L"htm", L"xml", L"json"
};

static VOID SarPhantomNameForIndex(_In_ const UCHAR Secret[32],
                                   _In_ PCUNICODE_STRING DirPath,
                                   _In_ ULONG Index,
                                   _Out_writes_(SAR_PHANTOM_NAME_CHARS) WCHAR *NameBuf,
                                   _Out_ PUSHORT NameChars)
{
    UINT8 msg[520 + 4];
    ULONG dir_bytes;
    const WCHAR *dir_buf;
    UINT8 hash[32];
    const ULONG ncons = RTL_NUMBER_OF(g_phantom_cons) - 1;
    const ULONG nvow  = RTL_NUMBER_OF(g_phantom_vow) - 1;
    ULONG hi = 0, nsyl, tmpl, s, eidx;
    USHORT pos = 0;
    const WCHAR *e;
    BOOLEAN cap;

    SarPhantomCanonDir(DirPath, &dir_buf, &dir_bytes);
    if (dir_bytes > 520)
        dir_bytes = 520;
    RtlCopyMemory(msg, dir_buf, dir_bytes);
    msg[dir_bytes]     = (UINT8)(Index & 0xFF);
    msg[dir_bytes + 1] = (UINT8)((Index >> 8) & 0xFF);
    msg[dir_bytes + 2] = (UINT8)((Index >> 16) & 0xFF);
    msg[dir_bytes + 3] = (UINT8)((Index >> 24) & 0xFF);
    sar_hmac_sha256(Secret, 32, msg, dir_bytes + 4, hash);

    cap = (hash[hi++ & 31] & 1) != 0;
    nsyl = 2 + (hash[hi++ & 31] % 3);
    for (s = 0; s < nsyl; s++) {
        WCHAR c = g_phantom_cons[hash[hi++ & 31] % ncons];
        WCHAR v = g_phantom_vow[hash[hi++ & 31] % nvow];
        if (s == 0 && cap)
            c = (WCHAR)(c - L'a' + L'A');
        NameBuf[pos++] = c;
        NameBuf[pos++] = v;
    }

    tmpl = hash[hi++ & 31] % 4;
    if (tmpl == 1 || tmpl == 2) {
        ULONG ndig, d;
        if (tmpl == 1)
            NameBuf[pos++] = L'_';
        ndig = 2 + (hash[hi++ & 31] % 4);
        for (d = 0; d < ndig; d++)
            NameBuf[pos++] = (WCHAR)(L'0' + (hash[hi++ & 31] % 10));
    } else if (tmpl == 3) {
        ULONG nsyl2 = 1 + (hash[hi++ & 31] % 2), s2;
        for (s2 = 0; s2 < nsyl2; s2++) {
            WCHAR c = g_phantom_cons[hash[hi++ & 31] % ncons];
            WCHAR v = g_phantom_vow[hash[hi++ & 31] % nvow];
            if (s2 == 0)
                c = (WCHAR)(c - L'a' + L'A');
            NameBuf[pos++] = c;
            NameBuf[pos++] = v;
        }
    }

    eidx = ((ULONG)hash[30] | ((ULONG)hash[31] << 8)) % RTL_NUMBER_OF(g_phantom_ext);
    NameBuf[pos++] = L'.';
    e = g_phantom_ext[eidx];
    while (*e)
        NameBuf[pos++] = *e++;
    *NameChars = pos;
}

static ULONG SarPhantomCountForDir(ULONG real_file_count)
{
    if (real_file_count == 0) return 0;
    if (real_file_count <= 5) return 1;
    if (real_file_count <= 20) return 2;
    return SAR_PHANTOM_MAX_PER_DIR;
}

static DWORDLONG SarPhantomSyntheticRef(ULONG dir_hash, ULONG index)
{
    return SAR_PHANTOM_SYNTH_REF_MASK |
           ((DWORDLONG)(dir_hash & 0xFFFF) << 32) |
           ((DWORDLONG)(index & 0xFFFF));
}

static SAR_PHANTOM_TRUST SarPhantomProcessTrust(_In_ HANDLE ProcessId)
{
    PSAR_IDENTITY_ENTRY entry;
    SAR_PHANTOM_TRUST trust = SAR_PHANTOM_UNTRUSTED;

    if (g_sar_state == NULL)
        return SAR_PHANTOM_UNTRUSTED;

    FltAcquirePushLockShared(&g_sar_state->identity_lock);
    {
        ULONG bucket = ((ULONG_PTR)ProcessId ^ ((ULONG_PTR)ProcessId >> 13)) * 0x9E3779B1u;
        bucket = bucket % g_sar_state->identity_bucket_count;
        PLIST_ENTRY head = &g_sar_state->identity_buckets[bucket];
        for (PLIST_ENTRY cur = head->Flink; cur != head; cur = cur->Flink) {
            entry = CONTAINING_RECORD(cur, SAR_IDENTITY_ENTRY, link);
            if (entry->process_id == ProcessId) {
                trust = entry->phantom_trust;
                break;
            }
        }
    }
    FltReleasePushLock(&g_sar_state->identity_lock);
    return trust;
}

static BOOLEAN SarPhantomIsTrusted(_In_ HANDLE ProcessId)
{
    return SarPhantomProcessTrust(ProcessId) == SAR_PHANTOM_TRUSTED;
}

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarPhantomIsTrustedProcess(_In_ HANDLE ProcessId)
{
    return SarPhantomIsTrusted(ProcessId);
}

static HANDLE SarPhantomCurrentPid(PFLT_CALLBACK_DATA Data)
{
    PETHREAD thread = Data->Thread;
    PEPROCESS proc;
    if (thread == NULL)
        thread = PsGetCurrentThread();
    proc = IoThreadToProcess(thread);
    if (proc == NULL)
        proc = PsGetCurrentProcess();
    return PsGetProcessId(proc);
}

typedef struct _SAR_PHANTOM_META {
    ULONG size;
    LARGE_INTEGER ctime;
    LARGE_INTEGER atime;
    LARGE_INTEGER wtime;
} SAR_PHANTOM_META;

static ULONGLONG SarPhantomRead64(_In_reads_(8) const UINT8 *p)
{
    return (ULONGLONG)p[0] | ((ULONGLONG)p[1] << 8) | ((ULONGLONG)p[2] << 16) | ((ULONGLONG)p[3] << 24) |
           ((ULONGLONG)p[4] << 32) | ((ULONGLONG)p[5] << 40) | ((ULONGLONG)p[6] << 48) | ((ULONGLONG)p[7] << 56);
}

static VOID SarPhantomMetaForIndex(_In_ const UCHAR Secret[32], _In_ PCUNICODE_STRING DirPath,
                                   _In_ ULONG Index, _Out_ SAR_PHANTOM_META *Meta)
{
    UINT8 msg[520 + 5];
    ULONG dir_bytes, r;
    const WCHAR *dir_buf;
    UINT8 hash[32];
    const ULONGLONG base = 132500000000000000ULL;
    const ULONGLONG span = 1576800000000000ULL;
    ULONGLONG a, b, c;

    SarPhantomCanonDir(DirPath, &dir_buf, &dir_bytes);
    if (dir_bytes > 520)
        dir_bytes = 520;
    RtlCopyMemory(msg, dir_buf, dir_bytes);
    msg[dir_bytes]     = (UINT8)(Index & 0xFF);
    msg[dir_bytes + 1] = (UINT8)((Index >> 8) & 0xFF);
    msg[dir_bytes + 2] = (UINT8)((Index >> 16) & 0xFF);
    msg[dir_bytes + 3] = (UINT8)((Index >> 24) & 0xFF);
    msg[dir_bytes + 4] = 0x4D;
    sar_hmac_sha256(Secret, 32, msg, dir_bytes + 5, hash);

    r = (ULONG)hash[0] | ((ULONG)hash[1] << 8) | ((ULONG)hash[2] << 16) | ((ULONG)hash[3] << 24);
    Meta->size = 4096 + (r % (1024 * 1024 - 4096));
    a = SarPhantomRead64(hash + 4)  % span;
    b = SarPhantomRead64(hash + 12) % (span / 4);
    c = SarPhantomRead64(hash + 20) % (span / 8);
    Meta->ctime.QuadPart = (LONGLONG)(base + a);
    Meta->wtime.QuadPart = (LONGLONG)(base + a + b);
    Meta->atime.QuadPart = (LONGLONG)(base + a + b + c);
}

static VOID SarPhantomFillEntry(_Out_writes_bytes_(entry_size) PVOID Entry,
                                _In_ const SAR_DIR_OFFSETS *off,
                                _In_ ULONG entry_size,
                                _In_ const WCHAR *Name,
                                _In_ USHORT NameChars,
                                _In_ const SAR_PHANTOM_META *Meta,
                                _In_ DWORDLONG SyntheticRef)
{
    ULONG name_bytes = NameChars * sizeof(WCHAR);

    RtlZeroMemory(Entry, entry_size);

    *(ULONG *)((UCHAR *)Entry + off->next_entry) = 0;

    if (off->creation_time != (ULONG)-1) {
        LARGE_INTEGER eof, alloc;
        eof.QuadPart = (LONGLONG)Meta->size;
        alloc.QuadPart = (eof.QuadPart + 4095) & ~4095LL;
        *(LARGE_INTEGER *)((UCHAR *)Entry + off->creation_time) = Meta->ctime;
        *(LARGE_INTEGER *)((UCHAR *)Entry + off->last_access)   = Meta->atime;
        *(LARGE_INTEGER *)((UCHAR *)Entry + off->last_write)    = Meta->wtime;
        *(LARGE_INTEGER *)((UCHAR *)Entry + off->change_time)   = Meta->wtime;
        *(LARGE_INTEGER *)((UCHAR *)Entry + off->end_of_file)   = eof;
        *(LARGE_INTEGER *)((UCHAR *)Entry + off->alloc_size)    = alloc;
        *(ULONG *)((UCHAR *)Entry + off->attrs) = FILE_ATTRIBUTE_ARCHIVE;
    }

    if (off->name_length != (ULONG)-1)
        *(ULONG *)((UCHAR *)Entry + off->name_length) = name_bytes;

    if (off->name_start != (ULONG)-1)
        RtlCopyMemory((UCHAR *)Entry + off->name_start, Name, name_bytes);

    if (off->ea_size >= 0)
        *(ULONG *)((UCHAR *)Entry + off->ea_size) = 0;

    if (off->short_name_length >= 0)
        *(UCHAR *)((UCHAR *)Entry + off->short_name_length) = 0;

    if (off->file_id >= 0)
        *(DWORDLONG *)((UCHAR *)Entry + off->file_id) = SyntheticRef;

    if (off->file_id_128 >= 0)
        *(DWORDLONG *)((UCHAR *)Entry + off->file_id_128) = SyntheticRef;
}

static ULONG SarPhantomCountRealEntries(_In_ PVOID Buffer, _In_ ULONG Length,
                                        _In_ const SAR_DIR_OFFSETS *off,
                                        _Out_ PVOID *LastEntry)
{
    ULONG count = 0;
    UCHAR *cur = (UCHAR *)Buffer;
    UCHAR *end = (UCHAR *)Buffer + Length;
    *LastEntry = NULL;

    while (cur < end) {
        ULONG next = *(ULONG *)(cur + off->next_entry);
        count++;
        *LastEntry = cur;
        if (next == 0)
            break;
        cur += next;
    }
    return count;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarPhantomCreate(_In_ PFLT_FILTER Filter, _Outptr_ PSAR_PHANTOM *Phantom)
{
    PSAR_PHANTOM ph;
    UNICODE_STRING name;

    *Phantom = NULL;

    ph = (PSAR_PHANTOM)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(SAR_PHANTOM),
                                       SAR_POOL_TAG_PHANTOM);
    if (ph == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    ph->filter = Filter;
    ph->active = FALSE;
    ph->image_notify_registered = FALSE;
    RtlZeroMemory(ph->volume_secret, SAR_PHANTOM_SECRET_BYTES);

    FltInitializePushLock(&g_pm_idmap_lock);
    RtlZeroMemory(g_pm_idmap, sizeof(g_pm_idmap));
    g_pm_idmap_next = 0;

    RtlInitUnicodeString(&name, L"IoReplaceFileObjectName");
    g_sar_io_replace_name = (PIO_REPLACE_FILE_OBJECT_NAME_FN)MmGetSystemRoutineAddress(&name);

    if (g_sar_io_replace_name == NULL) {
        ExFreePoolWithTag(ph, SAR_POOL_TAG_PHANTOM);
        return STATUS_NOT_SUPPORTED;
    }

    *Phantom = ph;
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarPhantomActivate(_Inout_ PSAR_PHANTOM Phantom, _In_ const UCHAR MacKey[32])
{
    SarPhantomDeriveSecret(MacKey, Phantom->volume_secret);
    SarStoreEnsureDir(SAR_PHANTOM_DIR, SAR_POOL_TAG_PHANTOM);
    Phantom->active = TRUE;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarPhantomDestroy(_Inout_ PSAR_PHANTOM Phantom)
{
    if (Phantom == NULL)
        return;
    Phantom->active = FALSE;
    sar_secure_zero(Phantom->volume_secret, SAR_PHANTOM_SECRET_BYTES);
    FltDeletePushLock(&g_pm_idmap_lock);
    ExFreePoolWithTag(Phantom, SAR_POOL_TAG_PHANTOM);
}

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarPhantomActive(_In_opt_ PSAR_PHANTOM Phantom)
{
    if (Phantom == NULL) return FALSE;
    return Phantom->active;
}

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarPhantomIsPhantomPath(_In_ PCUNICODE_STRING Path)
{
    static const WCHAR marker[] = L"\\phantom\\";
    USHORT nchars = (USHORT)(Path->Length / sizeof(WCHAR));
    USHORT mlen = (USHORT)(RTL_NUMBER_OF(marker) - 1);

    if (nchars < mlen)
        return FALSE;

    for (USHORT i = 0; (USHORT)(i + mlen) <= nchars; i++) {
        BOOLEAN match = TRUE;
        for (USHORT j = 0; j < mlen; j++) {
            WCHAR a = Path->Buffer[i + j];
            WCHAR b = marker[j];
            if (a >= L'A' && a <= L'Z') a = (WCHAR)(a + 32);
            if (b >= L'A' && b <= L'Z') b = (WCHAR)(b + 32);
            if (a != b) { match = FALSE; break; }
        }
        if (match) return TRUE;
    }
    return FALSE;
}

static BOOLEAN SarPhantomExtInPalette(_In_ PCUNICODE_STRING Name)
{
    USHORT chars = (USHORT)(Name->Length / sizeof(WCHAR));
    ULONG k;

    for (k = 0; k < RTL_NUMBER_OF(g_phantom_ext); k++) {
        const WCHAR *e = g_phantom_ext[k];
        USHORT elen = 0, j;
        const WCHAR *tail;

        while (e[elen])
            elen++;
        if (chars < (USHORT)(elen + 1))
            continue;
        if (Name->Buffer[chars - elen - 1] != L'.')
            continue;
        tail = Name->Buffer + (chars - elen);
        for (j = 0; j < elen; j++) {
            WCHAR c = tail[j];
            if (c >= L'A' && c <= L'Z')
                c = (WCHAR)(c + 32);
            if (c != e[j])
                break;
        }
        if (j == elen)
            return TRUE;
    }
    return FALSE;
}

static BOOLEAN SarPhantomMatchName(_In_ PCUNICODE_STRING FileName,
                                   _In_ PCUNICODE_STRING DirPath,
                                   _In_ PSAR_PHANTOM Phantom,
                                   _Out_ PULONG MatchIndex)
{
    ULONG max_phantoms = SAR_PHANTOM_MAX_PER_DIR;
    WCHAR gen_name[SAR_PHANTOM_NAME_CHARS];
    USHORT gen_chars;
    ULONG i;

    *MatchIndex = (ULONG)-1;

    for (i = 0; i < max_phantoms; i++) {
        UNICODE_STRING gen;
        SarPhantomNameForIndex(Phantom->volume_secret, DirPath, i, gen_name, &gen_chars);
        gen.Buffer = gen_name;
        gen.Length = (USHORT)(gen_chars * sizeof(WCHAR));
        gen.MaximumLength = gen.Length;
        if (RtlEqualUnicodeString(FileName, &gen, TRUE)) {
            *MatchIndex = i;
            return TRUE;
        }
    }
    return FALSE;
}

static VOID SarPhantomBuildBackingPath(_In_ ULONG PhantomIndex,
                                       _In_ PCUNICODE_STRING DirPath,
                                       _Out_writes_(260) WCHAR *BackingPath,
                                       _Out_ PUSHORT BackingChars)
{
    UINT8 hash_msg[524];
    UINT8 hash[32];
    ULONG dir_bytes;
    const WCHAR *dir_buf;
    static const WCHAR hex[] = L"0123456789abcdef";
    USHORT pos = 0;

    SarPhantomCanonDir(DirPath, &dir_buf, &dir_bytes);

    static const WCHAR prefix[] = L"\\SystemRoot\\System32\\drivers\\SemanticsAr\\phantom\\";
    USHORT prefix_chars = (USHORT)(RTL_NUMBER_OF(prefix) - 1);
    RtlCopyMemory(BackingPath, prefix, prefix_chars * sizeof(WCHAR));
    pos = prefix_chars;

    if (dir_bytes > 520) dir_bytes = 520;
    RtlCopyMemory(hash_msg, dir_buf, dir_bytes);
    hash_msg[dir_bytes]     = (UINT8)(PhantomIndex & 0xFF);
    hash_msg[dir_bytes + 1] = (UINT8)((PhantomIndex >> 8) & 0xFF);
    hash_msg[dir_bytes + 2] = (UINT8)((PhantomIndex >> 16) & 0xFF);
    hash_msg[dir_bytes + 3] = (UINT8)((PhantomIndex >> 24) & 0xFF);

    sar_hmac_sha256(g_sar.phantom->volume_secret, 32, hash_msg, dir_bytes + 4, hash);

    for (ULONG i = 0; i < 16 && pos < 258; i++) {
        BackingPath[pos++] = hex[(hash[i] >> 4) & 0xF];
        BackingPath[pos++] = hex[hash[i] & 0xF];
    }
    BackingPath[pos] = L'\0';
    *BackingChars = pos;
}

static const char g_pm_alpha[] = " etaoinshrdlcumwfgypbvkxqjz.,\r\n";
static const UINT8 g_pm_png[] = { 0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A };
static const UINT8 g_pm_jpg[] = { 0xFF,0xD8,0xFF,0xE0,0x00,0x10,0x4A,0x46,0x49,0x46,0x00 };
static const UINT8 g_pm_gif[] = { 0x47,0x49,0x46,0x38,0x39,0x61 };
static const UINT8 g_pm_bmp[] = { 0x42,0x4D };
static const UINT8 g_pm_pdf[] = { 0x25,0x50,0x44,0x46,0x2D,0x31,0x2E,0x37,0x0A };
static const UINT8 g_pm_zip[] = { 0x50,0x4B,0x03,0x04 };
static const UINT8 g_pm_ole[] = { 0xD0,0xCF,0x11,0xE0,0xA1,0xB1,0x1A,0xE1 };
static const UINT8 g_pm_rtf[] = { 0x7B,0x5C,0x72,0x74,0x66,0x31 };
static const UINT8 g_pm_gz[]  = { 0x1F,0x8B,0x08 };
static const UINT8 g_pm_7z[]  = { 0x37,0x7A,0xBC,0xAF,0x27,0x1C };
static const UINT8 g_pm_rar[] = { 0x52,0x61,0x72,0x21,0x1A,0x07,0x00 };

typedef struct _SAR_PM_ENTRY { const WCHAR *ext; const UINT8 *magic; ULONG len; } SAR_PM_ENTRY;
static const SAR_PM_ENTRY g_pm_table[] = {
    { L"png", g_pm_png, sizeof(g_pm_png) }, { L"jpg", g_pm_jpg, sizeof(g_pm_jpg) },
    { L"jpeg", g_pm_jpg, sizeof(g_pm_jpg) }, { L"gif", g_pm_gif, sizeof(g_pm_gif) },
    { L"bmp", g_pm_bmp, sizeof(g_pm_bmp) }, { L"pdf", g_pm_pdf, sizeof(g_pm_pdf) },
    { L"docx", g_pm_zip, sizeof(g_pm_zip) }, { L"xlsx", g_pm_zip, sizeof(g_pm_zip) },
    { L"pptx", g_pm_zip, sizeof(g_pm_zip) }, { L"zip", g_pm_zip, sizeof(g_pm_zip) },
    { L"odt", g_pm_zip, sizeof(g_pm_zip) }, { L"ods", g_pm_zip, sizeof(g_pm_zip) },
    { L"doc", g_pm_ole, sizeof(g_pm_ole) }, { L"xls", g_pm_ole, sizeof(g_pm_ole) },
    { L"ppt", g_pm_ole, sizeof(g_pm_ole) }, { L"mdb", g_pm_ole, sizeof(g_pm_ole) },
    { L"rtf", g_pm_rtf, sizeof(g_pm_rtf) }, { L"gz", g_pm_gz, sizeof(g_pm_gz) },
    { L"7z", g_pm_7z, sizeof(g_pm_7z) }, { L"rar", g_pm_rar, sizeof(g_pm_rar) }
};

static ULONG SarPhantomFillTemplate(_Out_writes_bytes_(BufLen) PUCHAR Buf, _In_ ULONG BufLen,
                                    _In_ PCUNICODE_STRING FinalComp)
{
    ULONG chars = FinalComp->Length / sizeof(WCHAR);
    LONG dot = -1;
    ULONG i;

    for (i = 0; i < chars; i++)
        if (FinalComp->Buffer[i] == L'.')
            dot = (LONG)i;
    if (dot < 0)
        return 0;

    for (i = 0; i < RTL_NUMBER_OF(g_pm_table); i++) {
        const WCHAR *e = g_pm_table[i].ext;
        ULONG elen = 0, j;
        while (e[elen]) elen++;
        if ((ULONG)(chars - dot - 1) != elen)
            continue;
        for (j = 0; j < elen; j++) {
            WCHAR c = FinalComp->Buffer[dot + 1 + j];
            if (c >= L'A' && c <= L'Z') c = (WCHAR)(c + 32);
            if (c != e[j]) break;
        }
        if (j == elen && g_pm_table[i].len <= BufLen) {
            RtlCopyMemory(Buf, g_pm_table[i].magic, g_pm_table[i].len);
            return g_pm_table[i].len;
        }
    }
    return 0;
}

static VOID SarPhantomSynthBody(_In_ const UCHAR Secret[32], _In_ PCUNICODE_STRING DirPath,
                               _In_ ULONG Index, _Out_writes_bytes_(Off + Len) PUCHAR Buf,
                               _In_ ULONG Off, _In_ ULONG Len)
{
    UINT8 msg[520 + 5];
    ULONG dir_bytes, i;
    const WCHAR *dir_buf;
    UINT8 hash[32];
    ULONGLONG state;
    const ULONG nalpha = sizeof(g_pm_alpha) - 1;

    SarPhantomCanonDir(DirPath, &dir_buf, &dir_bytes);
    if (dir_bytes > 520)
        dir_bytes = 520;
    RtlCopyMemory(msg, dir_buf, dir_bytes);
    msg[dir_bytes]     = (UINT8)(Index & 0xFF);
    msg[dir_bytes + 1] = (UINT8)((Index >> 8) & 0xFF);
    msg[dir_bytes + 2] = (UINT8)((Index >> 16) & 0xFF);
    msg[dir_bytes + 3] = (UINT8)((Index >> 24) & 0xFF);
    msg[dir_bytes + 4] = 0x42;
    sar_hmac_sha256(Secret, 32, msg, dir_bytes + 5, hash);
    state = SarPhantomRead64(hash);

    for (i = 0; i < Len; i++) {
        ULONGLONG z;
        state += 0x9E3779B97F4A7C15ULL;
        z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        z = z ^ (z >> 31);
        Buf[Off + i] = (UCHAR)g_pm_alpha[(ULONG)(z % nalpha)];
    }
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static VOID SarPhantomMaterializeBacking(_In_ PCWSTR BackingPath, _In_ PCUNICODE_STRING FinalComp,
                                         _In_ const UCHAR Secret[32], _In_ PCUNICODE_STRING DirPath,
                                         _In_ ULONG Index)
{
    UNICODE_STRING path;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    HANDLE h;
    NTSTATUS st;
    SAR_PHANTOM_META meta;
    PUCHAR buf;
    ULONG size, pos;
    FILE_BASIC_INFORMATION bi;
    LARGE_INTEGER off;

    SarPhantomMetaForIndex(Secret, DirPath, Index, &meta);
    size = meta.size;

    RtlInitUnicodeString(&path, BackingPath);
    InitializeObjectAttributes(&oa, &path, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

    st = ZwCreateFile(&h, FILE_READ_ATTRIBUTES, &oa, &iosb, NULL, FILE_ATTRIBUTE_NORMAL,
                      FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN,
                      FILE_NON_DIRECTORY_FILE, NULL, 0);
    if (NT_SUCCESS(st)) {
        ZwClose(h);
        return;
    }

    buf = (PUCHAR)ExAllocatePool2(POOL_FLAG_PAGED, size, SAR_POOL_TAG_PHANTOM);
    if (buf == NULL)
        return;
    pos = SarPhantomFillTemplate(buf, size, FinalComp);
    SarPhantomSynthBody(Secret, DirPath, Index, buf, pos, size - pos);

    st = ZwCreateFile(&h, FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE, &oa, &iosb, NULL,
                      FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OVERWRITE_IF,
                      FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    if (NT_SUCCESS(st)) {
        off.QuadPart = 0;
        ZwWriteFile(h, NULL, NULL, NULL, &iosb, buf, size, &off, NULL);
        RtlZeroMemory(&bi, sizeof(bi));
        bi.CreationTime = meta.ctime;
        bi.LastAccessTime = meta.atime;
        bi.LastWriteTime = meta.wtime;
        bi.ChangeTime = meta.wtime;
        ZwSetInformationFile(h, &iosb, &bi, sizeof(bi), FileBasicInformation);
        ZwClose(h);
    }
    ExFreePoolWithTag(buf, SAR_POOL_TAG_PHANTOM);
}

FLT_PREOP_CALLBACK_STATUS
SarPhantomPreCreate(_Inout_ PFLT_CALLBACK_DATA Data,
                    _In_ PCFLT_RELATED_OBJECTS FltObjects,
                    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext)
{
    PFILE_OBJECT fileObj;
    HANDLE pid;
    PFLT_FILE_NAME_INFORMATION nameInfo = NULL;
    ULONG matchIndex;
    WCHAR backingPath[260];
    USHORT backingChars;

    UNREFERENCED_PARAMETER(CompletionContext);

    if (Data->Iopb != NULL && Data->RequestorMode != KernelMode) {
        PETHREAD thread = Data->Thread;
        PEPROCESS process;
        if (thread == NULL)
            thread = PsGetCurrentThread();
        process = IoThreadToProcess(thread);
        if (process == NULL)
            process = PsGetCurrentProcess();
        if (SarCaptureOriginatorBlocked(g_sar.capture, process)) {
            ULONG disp = (Data->Iopb->Parameters.Create.Options >> 24) & 0xFF;
            if (disp == FILE_SUPERSEDE || disp == FILE_OVERWRITE || disp == FILE_OVERWRITE_IF) {
                Data->IoStatus.Status = STATUS_ACCESS_DENIED;
                Data->IoStatus.Information = 0;
                return FLT_PREOP_COMPLETE;
            }
        }
    }

    if (!SarPhantomActive(g_sar.phantom))
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;

    if (Data->RequestorMode == KernelMode)
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;

    fileObj = Data->Iopb->TargetFileObject;
    if (fileObj == NULL)
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;

    pid = SarPhantomCurrentPid(Data);

    if (fileObj->FileName.Length == 0 && fileObj->RelatedFileObject == NULL) {
        if (!SarPhantomIsTrusted(pid)) {
            Data->IoStatus.Status = STATUS_ACCESS_DENIED;
            Data->IoStatus.Information = 0;
            return FLT_PREOP_COMPLETE;
        }
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;
    }

    if (FlagOn(Data->Iopb->OperationFlags, SL_OPEN_TARGET_DIRECTORY))
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;

    if (!SarPhantomExtInPalette(&fileObj->FileName))
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;

    if (!NT_SUCCESS(FltGetFileNameInformation(Data,
            FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &nameInfo)))
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;

    if (!NT_SUCCESS(FltParseFileNameInformation(nameInfo))) {
        FltReleaseFileNameInformation(nameInfo);
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;
    }

    if (!SarPhantomMatchName(&nameInfo->FinalComponent, &nameInfo->ParentDir,
                             g_sar.phantom, &matchIndex)) {
        FltReleaseFileNameInformation(nameInfo);
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;
    }

    if (SarPhantomIsTrusted(pid)) {
        FltReleaseFileNameInformation(nameInfo);
        Data->IoStatus.Status = STATUS_OBJECT_NAME_NOT_FOUND;
        Data->IoStatus.Information = 0;
        return FLT_PREOP_COMPLETE;
    }

    if (((Data->Iopb->Parameters.Create.Options >> 24) & 0xFF) == FILE_CREATE) {
        FltReleaseFileNameInformation(nameInfo);
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;
    }

    SarPhantomBuildBackingPath(matchIndex, &nameInfo->ParentDir, backingPath, &backingChars);
    SarPhantomMaterializeBacking(backingPath, &nameInfo->FinalComponent,
                                 g_sar.phantom->volume_secret, &nameInfo->ParentDir, matchIndex);
    if (backingChars >= 32) {
        UNICODE_STRING backHex;
        backHex.Buffer = backingPath + (backingChars - 32);
        backHex.Length = 32 * sizeof(WCHAR);
        backHex.MaximumLength = backHex.Length;
        SarPmIdMapInsert(&backHex, &nameInfo->ParentDir, &nameInfo->FinalComponent, matchIndex);
    }
    FltReleaseFileNameInformation(nameInfo);

    if (g_sar_io_replace_name == NULL) {
        Data->IoStatus.Status = STATUS_OBJECT_NAME_NOT_FOUND;
        Data->IoStatus.Information = 0;
        return FLT_PREOP_COMPLETE;
    }

    g_sar_io_replace_name(fileObj, backingPath, backingChars * sizeof(WCHAR));
    Data->IoStatus.Status = STATUS_REPARSE;
    Data->IoStatus.Information = IO_REPARSE;
    FltSetCallbackDataDirty(Data);
    return FLT_PREOP_COMPLETE;
}

typedef struct _SAR_PHANTOM_DIR_CTX {
    FILE_INFORMATION_CLASS info_class;
    USHORT name_bytes;
    ULONG  match_mask;
    PSAR_PHANTOM_ENUM_CONTEXT ectx;
    PVOID  swapped_buffer;
    ULONG  buf_length;
    WCHAR  name[1];
} SAR_PHANTOM_DIR_CTX, *PSAR_PHANTOM_DIR_CTX;

static VOID SarPhantomCapturePattern(_In_ PFLT_CALLBACK_DATA Data, _Inout_ PSAR_PHANTOM_ENUM_CONTEXT Ectx)
{
    PUNICODE_STRING fn = Data->Iopb->Parameters.DirectoryControl.QueryDirectory.FileName;
    UNICODE_STRING dst;

    if (fn == NULL || fn->Length == 0 || fn->Buffer == NULL)
        return;

    dst.Buffer = Ectx->pattern;
    dst.Length = 0;
    dst.MaximumLength = sizeof(Ectx->pattern);
    if (NT_SUCCESS(RtlUpcaseUnicodeString(&dst, fn, FALSE))) {
        Ectx->pattern_bytes = dst.Length;
        Ectx->pattern_captured = TRUE;
    }
}

static ULONG SarPhantomComputeMask(_In_ PCUNICODE_STRING DirName, _In_ PSAR_PHANTOM_ENUM_CONTEXT Ectx)
{
    ULONG mask = 0, i;
    WCHAR nm[SAR_PHANTOM_NAME_CHARS];
    USHORT nc;
    UNICODE_STRING nameUS, patUS;

    if (!Ectx->pattern_captured)
        return (1u << SAR_PHANTOM_MAX_PER_DIR) - 1u;

    patUS.Buffer = Ectx->pattern;
    patUS.Length = Ectx->pattern_bytes;
    patUS.MaximumLength = Ectx->pattern_bytes;

    for (i = 0; i < SAR_PHANTOM_MAX_PER_DIR; i++) {
        SarPhantomNameForIndex(g_sar.phantom->volume_secret, DirName, i, nm, &nc);
        nameUS.Buffer = nm;
        nameUS.Length = (USHORT)(nc * sizeof(WCHAR));
        nameUS.MaximumLength = nameUS.Length;
        if (FsRtlIsNameInExpression(&patUS, &nameUS, TRUE, NULL))
            mask |= (1u << i);
    }
    return mask;
}

static ULONG SarPhantomMergeInto(_Out_writes_bytes_(BufLen) PUCHAR Out, _In_ ULONG BufLen,
                                 _In_reads_bytes_(Returned) PUCHAR Swapped, _In_ ULONG Returned,
                                 _In_ const SAR_DIR_OFFSETS *Off,
                                 _In_ const WCHAR *DirNameBuf, _In_ USHORT DirNameBytes,
                                 _In_ ULONG MatchMask, _Inout_ PSAR_PHANTOM_ENUM_CONTEXT Ectx,
                                 _Out_ PULONG EmittedOut)
{
    PVOID lastEntry;
    ULONG realCount, density, i, nOrder = 0, totalPhantom = 0, realTotal = 0, used, localMask = 0, emitted = 0;
    ULONG order[SAR_PHANTOM_MAX_PER_DIR];
    WCHAR phName[SAR_PHANTOM_MAX_PER_DIR][SAR_PHANTOM_NAME_CHARS];
    USHORT phChars[SAR_PHANTOM_MAX_PER_DIR];
    ULONG phSize[SAR_PHANTOM_MAX_PER_DIR];
    DWORDLONG phRef[SAR_PHANTOM_MAX_PER_DIR];
    SAR_PHANTOM_META phMeta[SAR_PHANTOM_MAX_PER_DIR];
    UNICODE_STRING dirName;
    PUCHAR rcur, end, lastOut;

    *EmittedOut = 0;

    realCount = SarPhantomCountRealEntries(Swapped, Returned, Off, &lastEntry);
    Ectx->real_seen += realCount;
    density = SarPhantomCountForDir(Ectx->real_seen);

    for (i = 0; i < SAR_PHANTOM_MAX_PER_DIR && nOrder < density; i++)
        if (MatchMask & (1u << i))
            order[nOrder++] = i;

    if (nOrder == 0 || realCount == 0) {
        RtlCopyMemory(Out, Swapped, Returned);
        return Returned;
    }

    dirName.Buffer = (PWCH)DirNameBuf;
    dirName.Length = DirNameBytes;
    dirName.MaximumLength = DirNameBytes;

    for (i = 0; i < nOrder; i++) {
        SarPhantomNameForIndex(g_sar.phantom->volume_secret, &dirName, order[i], phName[i], &phChars[i]);
        SarPhantomMetaForIndex(g_sar.phantom->volume_secret, &dirName, order[i], &phMeta[i]);
        phSize[i] = SarPhantomEntrySize(Off, phChars[i] * sizeof(WCHAR));
        phRef[i] = SarPhantomSyntheticRef((ULONG)(DirNameBytes / sizeof(WCHAR)), order[i]);
        totalPhantom += phSize[i];
    }

    end = Swapped + Returned;
    rcur = Swapped;
    while (rcur < end) {
        ULONG clen = Off->base_record_size + *(ULONG *)(rcur + Off->name_length);
        ULONG nxt = *(ULONG *)(rcur + Off->next_entry);
        realTotal += (clen + 7) & ~7u;
        if (nxt == 0)
            break;
        rcur += nxt;
    }

    if (realTotal + totalPhantom > BufLen) {
        RtlCopyMemory(Out, Swapped, Returned);
        return Returned;
    }

    used = 0;
    lastOut = NULL;
    rcur = Swapped;

    while (rcur != NULL || emitted < nOrder) {
        ULONG minPi = (ULONG)-1;
        BOOLEAN takePhantom;

        for (i = 0; i < nOrder; i++) {
            UNICODE_STRING a, b;
            if (localMask & (1u << i))
                continue;
            if (minPi == (ULONG)-1) {
                minPi = i;
                continue;
            }
            a.Buffer = phName[i];      a.Length = (USHORT)(phChars[i] * sizeof(WCHAR));      a.MaximumLength = a.Length;
            b.Buffer = phName[minPi];  b.Length = (USHORT)(phChars[minPi] * sizeof(WCHAR));  b.MaximumLength = b.Length;
            if (RtlCompareUnicodeString(&a, &b, TRUE) < 0)
                minPi = i;
        }

        if (rcur == NULL) {
            takePhantom = TRUE;
        } else if (minPi == (ULONG)-1) {
            takePhantom = FALSE;
        } else {
            UNICODE_STRING pn, rn;
            pn.Buffer = phName[minPi]; pn.Length = (USHORT)(phChars[minPi] * sizeof(WCHAR)); pn.MaximumLength = pn.Length;
            rn.Buffer = (PWCH)(rcur + Off->name_start);
            rn.Length = (USHORT)*(ULONG *)(rcur + Off->name_length);
            rn.MaximumLength = rn.Length;
            takePhantom = (RtlCompareUnicodeString(&pn, &rn, TRUE) < 0);
        }

        if (lastOut != NULL)
            *(ULONG *)(lastOut + Off->next_entry) = (ULONG)((Out + used) - lastOut);

        if (takePhantom) {
            SarPhantomFillEntry(Out + used, Off, phSize[minPi], phName[minPi], phChars[minPi],
                                &phMeta[minPi], phRef[minPi]);
            lastOut = Out + used;
            used += phSize[minPi];
            localMask |= (1u << minPi);
            emitted++;
        } else {
            ULONG clen = Off->base_record_size + *(ULONG *)(rcur + Off->name_length);
            ULONG esize = (clen + 7) & ~7u;
            ULONG nxt = *(ULONG *)(rcur + Off->next_entry);
            RtlCopyMemory(Out + used, rcur, clen);
            if (esize > clen)
                RtlZeroMemory(Out + used + clen, esize - clen);
            *(ULONG *)(Out + used + Off->next_entry) = 0;
            lastOut = Out + used;
            used += esize;
            rcur = (nxt == 0 || rcur + nxt >= end) ? NULL : (rcur + nxt);
        }
    }

    if (lastOut != NULL)
        *(ULONG *)(lastOut + Off->next_entry) = 0;

    *EmittedOut = emitted;
    return used;
}

static VOID SarPhantomDeliver(_Inout_ PFLT_CALLBACK_DATA Data, _In_ PSAR_PHANTOM_DIR_CTX Ctx,
                              _In_ PUCHAR OrigBuf)
{
    const SAR_DIR_OFFSETS *off = SarPhantomDirOffsets(Ctx->info_class);
    ULONG returned = (ULONG)Data->IoStatus.Information;
    ULONG usedBytes = returned, emitted = 0;

    __try {
        if (off != NULL && returned <= Ctx->buf_length && Ctx->swapped_buffer != NULL && Ctx->ectx != NULL)
            usedBytes = SarPhantomMergeInto(OrigBuf, Ctx->buf_length, (PUCHAR)Ctx->swapped_buffer, returned, off,
                                            Ctx->name, Ctx->name_bytes, Ctx->match_mask, Ctx->ectx, &emitted);
        else
            RtlCopyMemory(OrigBuf, Ctx->swapped_buffer, Ctx->buf_length);
        Data->IoStatus.Information = usedBytes;
        if (emitted > 0 && Ctx->ectx != NULL)
            Ctx->ectx->emitted = TRUE;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Data->IoStatus.Status = GetExceptionCode();
        Data->IoStatus.Information = 0;
    }
}

static VOID SarPhantomFreeDirCtx(_In_ PSAR_PHANTOM_DIR_CTX Ctx)
{
    if (Ctx->ectx != NULL)
        FltReleaseContext(Ctx->ectx);
    if (Ctx->swapped_buffer != NULL)
        ExFreePoolWithTag(Ctx->swapped_buffer, SAR_POOL_TAG_PHANTOM);
    ExFreePoolWithTag(Ctx, SAR_POOL_TAG_PHANTOM);
}

_Function_class_(PFLT_POST_OPERATION_CALLBACK)
static FLT_POSTOP_CALLBACK_STATUS
SarPhantomPostDirCtrlWhenSafe(_Inout_ PFLT_CALLBACK_DATA Data, _In_ PCFLT_RELATED_OBJECTS FltObjects,
                              _In_opt_ PVOID CompletionContext, _In_ FLT_POST_OPERATION_FLAGS Flags)
{
    PSAR_PHANTOM_DIR_CTX ctx = (PSAR_PHANTOM_DIR_CTX)CompletionContext;
    PUCHAR origBuf;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Flags);

    status = FltLockUserBuffer(Data);
    if (NT_SUCCESS(status)) {
        origBuf = (PUCHAR)MmGetSystemAddressForMdlSafe(
            Data->Iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress,
            NormalPagePriority | MdlMappingNoExecute);
        if (origBuf != NULL) {
            SarPhantomDeliver(Data, ctx, origBuf);
        } else {
            Data->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
            Data->IoStatus.Information = 0;
        }
    } else {
        Data->IoStatus.Status = status;
        Data->IoStatus.Information = 0;
    }
    SarPhantomFreeDirCtx(ctx);
    return FLT_POSTOP_FINISHED_PROCESSING;
}

FLT_PREOP_CALLBACK_STATUS
SarPhantomPreDirControl(_Inout_ PFLT_CALLBACK_DATA Data,
                        _In_ PCFLT_RELATED_OBJECTS FltObjects,
                        _Flt_CompletionContext_Outptr_ PVOID *CompletionContext)
{
    HANDLE pid;
    FILE_INFORMATION_CLASS infoClass;
    PFLT_FILE_NAME_INFORMATION nameInfo = NULL;
    PSAR_PHANTOM_DIR_CTX ctx;
    USHORT nb;

    *CompletionContext = NULL;

    if (!SarPhantomActive(g_sar.phantom))
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    if (Data->Iopb == NULL)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    if (Data->RequestorMode == KernelMode)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    if (Data->Iopb->MinorFunction != IRP_MN_QUERY_DIRECTORY)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    if (FlagOn(Data->Iopb->OperationFlags, SL_RETURN_SINGLE_ENTRY))
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    infoClass = Data->Iopb->Parameters.DirectoryControl.QueryDirectory.FileInformationClass;
    if (SarPhantomDirOffsets(infoClass) == NULL)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    pid = SarPhantomCurrentPid(Data);
    if (SarPhantomIsTrusted(pid))
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    if (!NT_SUCCESS(FltGetFileNameInformation(Data,
            FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_CACHE_ONLY, &nameInfo)))
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    if (!NT_SUCCESS(FltParseFileNameInformation(nameInfo))) {
        FltReleaseFileNameInformation(nameInfo);
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    {
        PSAR_PHANTOM_ENUM_CONTEXT ectx = NULL;
        ULONG mask;
        ULONG len = Data->Iopb->Parameters.DirectoryControl.QueryDirectory.Length;
        PVOID newBuf;
        PMDL newMdl;

        if (!NT_SUCCESS(FltGetStreamHandleContext(FltObjects->Instance, FltObjects->FileObject,
                                                  (PFLT_CONTEXT *)&ectx))) {
            PSAR_PHANTOM_ENUM_CONTEXT newE = NULL;
            if (NT_SUCCESS(FltAllocateContext(g_sar.phantom->filter, FLT_STREAMHANDLE_CONTEXT,
                                              sizeof(SAR_PHANTOM_ENUM_CONTEXT), NonPagedPoolNx,
                                              (PFLT_CONTEXT *)&newE))) {
                PSAR_PHANTOM_ENUM_CONTEXT old = NULL;
                NTSTATUS sst;
                RtlZeroMemory(newE, sizeof(SAR_PHANTOM_ENUM_CONTEXT));
                SarPhantomCapturePattern(Data, newE);
                sst = FltSetStreamHandleContext(FltObjects->Instance, FltObjects->FileObject,
                                                FLT_SET_CONTEXT_KEEP_IF_EXISTS, newE, (PFLT_CONTEXT *)&old);
                if (sst == STATUS_FLT_CONTEXT_ALREADY_DEFINED) {
                    FltReleaseContext(newE);
                    ectx = old;
                } else if (NT_SUCCESS(sst)) {
                    ectx = newE;
                } else {
                    FltReleaseContext(newE);
                    ectx = NULL;
                }
            }
        }
        if (ectx == NULL) {
            FltReleaseFileNameInformation(nameInfo);
            return FLT_PREOP_SUCCESS_NO_CALLBACK;
        }

        if (FlagOn(Data->Iopb->OperationFlags, SL_RESTART_SCAN)) {
            ectx->emitted = FALSE;
            ectx->real_seen = 0;
            if (Data->Iopb->Parameters.DirectoryControl.QueryDirectory.FileName != NULL &&
                Data->Iopb->Parameters.DirectoryControl.QueryDirectory.FileName->Length > 0) {
                ectx->pattern_captured = FALSE;
                SarPhantomCapturePattern(Data, ectx);
            }
        }

        if (ectx->emitted || len == 0) {
            FltReleaseContext(ectx);
            FltReleaseFileNameInformation(nameInfo);
            return FLT_PREOP_SUCCESS_NO_CALLBACK;
        }

        mask = SarPhantomComputeMask(&nameInfo->Name, ectx);
        if (mask == 0) {
            FltReleaseContext(ectx);
            FltReleaseFileNameInformation(nameInfo);
            return FLT_PREOP_SUCCESS_NO_CALLBACK;
        }

        nb = nameInfo->Name.Length;
        newBuf = ExAllocatePool2(POOL_FLAG_NON_PAGED, len, SAR_POOL_TAG_PHANTOM);
        if (newBuf == NULL) {
            FltReleaseContext(ectx);
            FltReleaseFileNameInformation(nameInfo);
            return FLT_PREOP_SUCCESS_NO_CALLBACK;
        }
        newMdl = IoAllocateMdl(newBuf, len, FALSE, FALSE, NULL);
        if (newMdl == NULL) {
            ExFreePoolWithTag(newBuf, SAR_POOL_TAG_PHANTOM);
            FltReleaseContext(ectx);
            FltReleaseFileNameInformation(nameInfo);
            return FLT_PREOP_SUCCESS_NO_CALLBACK;
        }
        MmBuildMdlForNonPagedPool(newMdl);

        ctx = (PSAR_PHANTOM_DIR_CTX)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                  sizeof(SAR_PHANTOM_DIR_CTX) + nb, SAR_POOL_TAG_PHANTOM);
        if (ctx == NULL) {
            IoFreeMdl(newMdl);
            ExFreePoolWithTag(newBuf, SAR_POOL_TAG_PHANTOM);
            FltReleaseContext(ectx);
            FltReleaseFileNameInformation(nameInfo);
            return FLT_PREOP_SUCCESS_NO_CALLBACK;
        }
        ctx->info_class = infoClass;
        ctx->name_bytes = nb;
        ctx->match_mask = mask;
        ctx->ectx = ectx;
        ctx->swapped_buffer = newBuf;
        ctx->buf_length = len;
        if (nb > 0)
            RtlCopyMemory(ctx->name, nameInfo->Name.Buffer, nb);
        FltReleaseFileNameInformation(nameInfo);

        Data->Iopb->Parameters.DirectoryControl.QueryDirectory.DirectoryBuffer = newBuf;
        Data->Iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress = newMdl;
        FltSetCallbackDataDirty(Data);
        *CompletionContext = ctx;
    }
    return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}

FLT_POSTOP_CALLBACK_STATUS
SarPhantomPostDirControl(_Inout_ PFLT_CALLBACK_DATA Data,
                         _In_ PCFLT_RELATED_OBJECTS FltObjects,
                         _In_opt_ PVOID CompletionContext,
                         _In_ FLT_POST_OPERATION_FLAGS Flags)
{
    PSAR_PHANTOM_DIR_CTX ctx = (PSAR_PHANTOM_DIR_CTX)CompletionContext;
    PUCHAR origBuf;
    FLT_POSTOP_CALLBACK_STATUS ret = FLT_POSTOP_FINISHED_PROCESSING;
    BOOLEAN cleanup = TRUE;

    UNREFERENCED_PARAMETER(FltObjects);

    if (ctx == NULL)
        return FLT_POSTOP_FINISHED_PROCESSING;

    if (FlagOn(Flags, FLTFL_POST_OPERATION_DRAINING))
        goto done;
    if (!NT_SUCCESS(Data->IoStatus.Status) || Data->IoStatus.Information == 0)
        goto done;
    if ((ULONG)Data->IoStatus.Information > ctx->buf_length)
        goto done;

    if (Data->Iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress != NULL) {
        origBuf = (PUCHAR)MmGetSystemAddressForMdlSafe(
            Data->Iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress,
            NormalPagePriority | MdlMappingNoExecute);
        if (origBuf != NULL) {
            SarPhantomDeliver(Data, ctx, origBuf);
        } else {
            Data->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
            Data->IoStatus.Information = 0;
        }
    } else {
        if (FltDoCompletionProcessingWhenSafe(Data, FltObjects, CompletionContext, Flags,
                                              SarPhantomPostDirCtrlWhenSafe, &ret)) {
            cleanup = FALSE;
        } else {
            Data->IoStatus.Status = STATUS_UNSUCCESSFUL;
            Data->IoStatus.Information = 0;
        }
    }

done:
    if (cleanup)
        SarPhantomFreeDirCtx(ctx);
    return ret;
}

FLT_POSTOP_CALLBACK_STATUS
SarPhantomPostQueryInfo(_Inout_ PFLT_CALLBACK_DATA Data,
                        _In_ PCFLT_RELATED_OBJECTS FltObjects,
                        _In_opt_ PVOID CompletionContext,
                        _In_ FLT_POST_OPERATION_FLAGS Flags)
{
    PFLT_FILE_NAME_INFORMATION nameInfo = NULL;
    FILE_INFORMATION_CLASS infoClass;
    HANDLE pid;

    UNREFERENCED_PARAMETER(CompletionContext);

    if (FlagOn(Flags, FLTFL_POST_OPERATION_DRAINING))
        return FLT_POSTOP_FINISHED_PROCESSING;
    if (!SarPhantomActive(g_sar.phantom))
        return FLT_POSTOP_FINISHED_PROCESSING;
    if (Data->RequestorMode == KernelMode)
        return FLT_POSTOP_FINISHED_PROCESSING;
    if (!NT_SUCCESS(Data->IoStatus.Status))
        return FLT_POSTOP_FINISHED_PROCESSING;
    if (Data->Iopb == NULL)
        return FLT_POSTOP_FINISHED_PROCESSING;

    if (FltObjects->FileObject == NULL ||
        !SarPhantomIsPhantomPath(&FltObjects->FileObject->FileName))
        return FLT_POSTOP_FINISHED_PROCESSING;

    pid = SarPhantomCurrentPid(Data);
    if (SarPhantomIsTrusted(pid))
        return FLT_POSTOP_FINISHED_PROCESSING;

    if (!NT_SUCCESS(FltGetFileNameInformation(Data,
            FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &nameInfo)))
        return FLT_POSTOP_FINISHED_PROCESSING;

    if (!NT_SUCCESS(FltParseFileNameInformation(nameInfo)) ||
        !SarPhantomIsPhantomPath(&nameInfo->Name)) {
        FltReleaseFileNameInformation(nameInfo);
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    infoClass = Data->Iopb->Parameters.QueryFileInformation.FileInformationClass;

    {
        WCHAR origDir[284];
        WCHAR phName[SAR_PHANTOM_NAME_CHARS];
        USHORT origDirLen = 0, phLen = 0;
        ULONG idx = 0;

        if (SarPmIdMapLookup(&nameInfo->FinalComponent, origDir, &origDirLen, phName, &phLen, &idx)) {
            if (infoClass == FileNameInformation || infoClass == FileNormalizedNameInformation) {
                PFILE_NAME_INFORMATION info =
                    (PFILE_NAME_INFORMATION)Data->Iopb->Parameters.QueryFileInformation.InfoBuffer;
                ULONG needed = (ULONG)origDirLen + (ULONG)phLen;
                ULONG bufCap = Data->Iopb->Parameters.QueryFileInformation.Length
                               - FIELD_OFFSET(FILE_NAME_INFORMATION, FileName);
                if (info != NULL && needed <= bufCap) {
                    RtlCopyMemory(info->FileName, origDir, origDirLen);
                    RtlCopyMemory((UCHAR *)info->FileName + origDirLen, phName, phLen);
                    info->FileNameLength = needed;
                    Data->IoStatus.Information = FIELD_OFFSET(FILE_NAME_INFORMATION, FileName) + needed;
                    FltSetCallbackDataDirty(Data);
                }
            } else if (infoClass == FileInternalInformation) {
                PFILE_INTERNAL_INFORMATION info =
                    (PFILE_INTERNAL_INFORMATION)Data->Iopb->Parameters.QueryFileInformation.InfoBuffer;
                if (info != NULL) {
                    ULONG dir_hash = (ULONG)(origDirLen / sizeof(WCHAR));
                    info->IndexNumber.QuadPart = (LONGLONG)SarPhantomSyntheticRef(dir_hash, idx);
                    FltSetCallbackDataDirty(Data);
                }
            }
        }
    }

    FltReleaseFileNameInformation(nameInfo);
    return FLT_POSTOP_FINISHED_PROCESSING;
}

FLT_POSTOP_CALLBACK_STATUS
SarPhantomPostFsControl(_Inout_ PFLT_CALLBACK_DATA Data,
                        _In_ PCFLT_RELATED_OBJECTS FltObjects,
                        _In_opt_ PVOID CompletionContext,
                        _In_ FLT_POST_OPERATION_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(CompletionContext);

    if (FlagOn(Flags, FLTFL_POST_OPERATION_DRAINING))
        return FLT_POSTOP_FINISHED_PROCESSING;
    if (!SarPhantomActive(g_sar.phantom))
        return FLT_POSTOP_FINISHED_PROCESSING;
    if (!NT_SUCCESS(Data->IoStatus.Status))
        return FLT_POSTOP_FINISHED_PROCESSING;
    if (Data->Iopb == NULL)
        return FLT_POSTOP_FINISHED_PROCESSING;

    return FLT_POSTOP_FINISHED_PROCESSING;
}

_IRQL_requires_max_(APC_LEVEL)
VOID SarPhantomRecordEvidence(_In_ HANDLE ProcessId)
{
    if (g_sar_state == NULL)
        return;

    FltAcquirePushLockShared(&g_sar_state->identity_lock);
    {
        ULONG bucket = ((ULONG_PTR)ProcessId ^ ((ULONG_PTR)ProcessId >> 13)) * 0x9E3779B1u;
        bucket = bucket % g_sar_state->identity_bucket_count;
        PLIST_ENTRY head = &g_sar_state->identity_buckets[bucket];
        for (PLIST_ENTRY cur = head->Flink; cur != head; cur = cur->Flink) {
            PSAR_IDENTITY_ENTRY entry = CONTAINING_RECORD(cur, SAR_IDENTITY_ENTRY, link);
            if (entry->process_id == ProcessId) {
                InterlockedIncrement(&entry->phantom_evidence);
                break;
            }
        }
    }
    FltReleasePushLock(&g_sar_state->identity_lock);
}

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarPhantomIsConvicted(_In_ HANDLE ProcessId)
{
    if (g_sar_state == NULL)
        return FALSE;

    LONG evidence = 0;
    FltAcquirePushLockShared(&g_sar_state->identity_lock);
    {
        ULONG bucket = ((ULONG_PTR)ProcessId ^ ((ULONG_PTR)ProcessId >> 13)) * 0x9E3779B1u;
        bucket = bucket % g_sar_state->identity_bucket_count;
        PLIST_ENTRY head = &g_sar_state->identity_buckets[bucket];
        for (PLIST_ENTRY cur = head->Flink; cur != head; cur = cur->Flink) {
            PSAR_IDENTITY_ENTRY entry = CONTAINING_RECORD(cur, SAR_IDENTITY_ENTRY, link);
            if (entry->process_id == ProcessId) {
                evidence = entry->phantom_evidence;
                break;
            }
        }
    }
    FltReleasePushLock(&g_sar_state->identity_lock);
    return evidence >= SAR_PHANTOM_K_THRESHOLD;
}

static VOID SarPhantomLoadImageNotify(_In_opt_ PUNICODE_STRING FullImageName,
                                      _In_ HANDLE ProcessId,
                                      _In_ PIMAGE_INFO ImageInfo)
{
    UNREFERENCED_PARAMETER(FullImageName);

    if (g_sar_state == NULL)
        return;
    if (ImageInfo->SystemModeImage)
        return;
    if (ProcessId == NULL)
        return;

    ULONG sigLevel = (ImageInfo->Properties >> 12) & 0xF;
    if (sigLevel >= 4)
        return;

    FltAcquirePushLockExclusive(&g_sar_state->identity_lock);
    {
        ULONG bucket = ((ULONG_PTR)ProcessId ^ ((ULONG_PTR)ProcessId >> 13)) * 0x9E3779B1u;
        bucket = bucket % g_sar_state->identity_bucket_count;
        PLIST_ENTRY head = &g_sar_state->identity_buckets[bucket];
        for (PLIST_ENTRY cur = head->Flink; cur != head; cur = cur->Flink) {
            PSAR_IDENTITY_ENTRY entry = CONTAINING_RECORD(cur, SAR_IDENTITY_ENTRY, link);
            if (entry->process_id == ProcessId) {
                if (entry->phantom_trust == SAR_PHANTOM_TRUSTED)
                    entry->phantom_trust = SAR_PHANTOM_TAINTED;
                break;
            }
        }
    }
    FltReleasePushLock(&g_sar_state->identity_lock);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarPhantomImageNotifyRegister(VOID)
{
    if (g_sar.phantom == NULL)
        return;
    if (NT_SUCCESS(PsSetLoadImageNotifyRoutine(SarPhantomLoadImageNotify)))
        g_sar.phantom->image_notify_registered = TRUE;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarPhantomImageNotifyUnregister(VOID)
{
    if (g_sar.phantom == NULL)
        return;
    if (g_sar.phantom->image_notify_registered) {
        PsRemoveLoadImageNotifyRoutine(SarPhantomLoadImageNotify);
        g_sar.phantom->image_notify_registered = FALSE;
    }
}
