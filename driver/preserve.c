#include "preserve.h"
#include "store_io.h"

#include <ntifs.h>
#include <bcrypt.h>

#include "sar_preserve.h"

#define SAR_PRES_DIR      L"\\SystemRoot\\System32\\drivers\\SemanticsAr"
#define SAR_PRES_INDEX    L"\\SystemRoot\\System32\\drivers\\SemanticsAr\\preserve.idx"
#define SAR_PRES_INDEXTMP L"\\SystemRoot\\System32\\drivers\\SemanticsAr\\preserve.tmp"
#define SAR_PRES_DATA     L"\\SystemRoot\\System32\\drivers\\SemanticsAr\\preserve.dat"
#define SAR_PRES_KEYFILE  L"\\SystemRoot\\System32\\drivers\\SemanticsAr\\preskey.bin"
#define SAR_PRES_KEYTMP   L"\\SystemRoot\\System32\\drivers\\SemanticsAr\\preskey.tmp"

#define SAR_PRESKEY_MAGIC 0x4B455250u

#pragma pack(push, 1)
typedef struct _SAR_PRESKEY_BLOB {
    UINT32 magic;
    UCHAR  store_key[SEMANTICS_AR_MAC_SIZE];
    UCHAR  mac_key[SEMANTICS_AR_MAC_SIZE];
    UCHAR  anchor_present;
    UCHAR  reserved[7];
    UINT64 anchor_generation;
    UCHAR  anchor_head_mac[SEMANTICS_AR_MAC_SIZE];
    UINT64 retention_100ns;
    UINT64 capacity_bytes;
} SAR_PRESKEY_BLOB, *PSAR_PRESKEY_BLOB;
#pragma pack(pop)

typedef struct _SAR_PRES_EXTENT {
    UINT64 off;
    UINT64 len;
} SAR_PRES_EXTENT;

struct _SAR_PRESERVE {
    PFLT_FILTER filter;
    PSAR_POSTURE posture;

    EX_PUSH_LOCK lock;
    sar_preserve_record_t *records;
    ULONG64 record_count;
    ULONG64 record_capacity;

    SAR_PRES_EXTENT *free_ext;
    ULONG free_count;
    ULONG free_capacity;
    UINT64 data_high_water;
    BOOLEAN free_dirty;
    HANDLE data_handle;

    BCRYPT_ALG_HANDLE aes_alg;
    ULONG key_obj_len;
    UCHAR store_key[SEMANTICS_AR_MAC_SIZE];
    UCHAR mac_key[SEMANTICS_AR_MAC_SIZE];
    BOOLEAN have_key;
    UINT64 generation;
    sar_keystore_anchor_t anchor;

    UINT64 retention_100ns;
    UINT64 capacity_bytes;

    volatile LONG ready;
    volatile LONG dirty;

    KEVENT stop;
    PETHREAD persist_thread;
    BOOLEAN thread_started;
};

static UINT64 SarPresRound16(UINT64 v)
{
    return (v + (SAR_PRESERVE_AES_BLOCK - 1)) & ~((UINT64)(SAR_PRESERVE_AES_BLOCK - 1));
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS SarPresCrypt(_In_ PSAR_PRESERVE P, _In_ BOOLEAN Encrypt,
                             _In_reads_bytes_(InLen) const UCHAR *In, _In_ ULONG InLen,
                             _In_reads_bytes_(SAR_PRESERVE_AES_BLOCK) const UCHAR *Iv,
                             _Out_writes_bytes_(OutCap) PUCHAR Out, _In_ ULONG OutCap,
                             _Out_ PULONG OutLen)
{
    BCRYPT_KEY_HANDLE hkey = NULL;
    PUCHAR key_obj;
    UCHAR iv_copy[SAR_PRESERVE_AES_BLOCK];
    ULONG result = 0;
    NTSTATUS status;

    *OutLen = 0;
    if ((InLen % SAR_PRESERVE_AES_BLOCK) != 0)
        return STATUS_INVALID_PARAMETER;

    key_obj = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, P->key_obj_len, SAR_POOL_TAG_PRESKEY);
    if (key_obj == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    status = BCryptGenerateSymmetricKey(P->aes_alg, &hkey, key_obj, P->key_obj_len,
                                        (PUCHAR)P->store_key, SEMANTICS_AR_MAC_SIZE, 0);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(key_obj, SAR_POOL_TAG_PRESKEY);
        return status;
    }

    RtlCopyMemory(iv_copy, Iv, SAR_PRESERVE_AES_BLOCK);
    if (Encrypt)
        status = BCryptEncrypt(hkey, (PUCHAR)In, InLen, NULL, iv_copy, SAR_PRESERVE_AES_BLOCK,
                               Out, OutCap, &result, 0);
    else
        status = BCryptDecrypt(hkey, (PUCHAR)In, InLen, NULL, iv_copy, SAR_PRESERVE_AES_BLOCK,
                               Out, OutCap, &result, 0);

    BCryptDestroyKey(hkey);
    RtlSecureZeroMemory(key_obj, P->key_obj_len);
    ExFreePoolWithTag(key_obj, SAR_POOL_TAG_PRESKEY);
    RtlSecureZeroMemory(iv_copy, sizeof(iv_copy));

    if (NT_SUCCESS(status))
        *OutLen = result;
    return status;
}

static VOID SarPresHeapSort(_Inout_updates_(N) SAR_PRES_EXTENT *A, _In_ ULONG N)
{
    LONG i;

    for (i = (LONG)(N / 2) - 1; i >= 0; i--) {
        ULONG root = (ULONG)i;
        for (;;) {
            ULONG child = 2 * root + 1;
            ULONG swap = root;
            if (child < N && A[swap].off < A[child].off)
                swap = child;
            if (child + 1 < N && A[swap].off < A[child + 1].off)
                swap = child + 1;
            if (swap == root)
                break;
            { SAR_PRES_EXTENT t = A[root]; A[root] = A[swap]; A[swap] = t; root = swap; }
        }
    }
    for (i = (LONG)N - 1; i > 0; i--) {
        SAR_PRES_EXTENT t = A[0]; A[0] = A[i]; A[i] = t;
        {
            ULONG root = 0, end = (ULONG)i;
            for (;;) {
                ULONG child = 2 * root + 1;
                ULONG swap = root;
                if (child < end && A[swap].off < A[child].off)
                    swap = child;
                if (child + 1 < end && A[swap].off < A[child + 1].off)
                    swap = child + 1;
                if (swap == root)
                    break;
                { SAR_PRES_EXTENT u = A[root]; A[root] = A[swap]; A[swap] = u; root = swap; }
            }
        }
    }
}

static VOID SarPresFreeRebuild(_Inout_ PSAR_PRESERVE P)
{
    ULONG n = (ULONG)P->record_count;
    SAR_PRES_EXTENT *a;
    ULONG i;
    UINT64 prev_end = 0;

    P->free_count = 0;

    if (n == 0) {
        P->data_high_water = 0;
        P->free_dirty = FALSE;
        return;
    }

    a = (SAR_PRES_EXTENT *)ExAllocatePool2(POOL_FLAG_PAGED, (SIZE_T)n * sizeof(SAR_PRES_EXTENT),
                                           SAR_POOL_TAG_PRESFREE);
    if (a == NULL)
        return;

    for (i = 0; i < n; i++) {
        a[i].off = P->records[i].payload_offset;
        a[i].len = P->records[i].payload_length;
    }
    SarPresHeapSort(a, n);

    for (i = 0; i < n; i++) {
        if (a[i].off > prev_end && P->free_count < P->free_capacity) {
            P->free_ext[P->free_count].off = prev_end;
            P->free_ext[P->free_count].len = a[i].off - prev_end;
            P->free_count++;
        }
        if (a[i].off + a[i].len > prev_end)
            prev_end = a[i].off + a[i].len;
    }
    P->data_high_water = prev_end;
    P->free_dirty = FALSE;
    ExFreePoolWithTag(a, SAR_POOL_TAG_PRESFREE);
}

static UINT64 SarPresAlloc(_Inout_ PSAR_PRESERVE P, _In_ UINT64 Len)
{
    ULONG i;
    UINT64 off;

    if (P->free_dirty)
        SarPresFreeRebuild(P);

    for (i = 0; i < P->free_count; i++) {
        if (P->free_ext[i].len >= Len) {
            off = P->free_ext[i].off;
            P->free_ext[i].off += Len;
            P->free_ext[i].len -= Len;
            if (P->free_ext[i].len == 0) {
                P->free_ext[i] = P->free_ext[P->free_count - 1];
                P->free_count--;
            }
            return off;
        }
    }

    off = P->data_high_water;
    P->data_high_water += Len;
    return off;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS SarPresDataWrite(_In_ PSAR_PRESERVE P, _In_ UINT64 Offset,
                                 _In_reads_bytes_(Len) const VOID *Buf, _In_ ULONG Len)
{
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER off;

    if (P->data_handle == NULL)
        return STATUS_DEVICE_NOT_READY;
    off.QuadPart = (LONGLONG)Offset;
    return ZwWriteFile(P->data_handle, NULL, NULL, NULL, &iosb, (PVOID)(ULONG_PTR)Buf, Len,
                       &off, NULL);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS SarPresDataRead(_In_ PSAR_PRESERVE P, _In_ UINT64 Offset,
                                _Out_writes_bytes_(Len) PVOID Buf, _In_ ULONG Len)
{
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER off;

    if (P->data_handle == NULL)
        return STATUS_DEVICE_NOT_READY;
    off.QuadPart = (LONGLONG)Offset;
    return ZwReadFile(P->data_handle, NULL, NULL, NULL, &iosb, Buf, Len, &off, NULL);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static VOID SarPresFillBlob(_Out_ PSAR_PRESKEY_BLOB Blob, _In_ PSAR_PRESERVE P)
{
    RtlZeroMemory(Blob, sizeof(*Blob));
    Blob->magic = SAR_PRESKEY_MAGIC;
    RtlCopyMemory(Blob->store_key, P->store_key, SEMANTICS_AR_MAC_SIZE);
    RtlCopyMemory(Blob->mac_key, P->mac_key, SEMANTICS_AR_MAC_SIZE);
    Blob->anchor_present = (UCHAR)(P->anchor.present ? 1 : 0);
    Blob->anchor_generation = P->anchor.generation;
    RtlCopyMemory(Blob->anchor_head_mac, P->anchor.head_mac, SEMANTICS_AR_MAC_SIZE);
    Blob->retention_100ns = P->retention_100ns;
    Blob->capacity_bytes = P->capacity_bytes;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS SarPresWriteBlob(_In_ PSAR_PRESERVE P)
{
    SAR_PRESKEY_BLOB blob;
    NTSTATUS status;

    SarPresFillBlob(&blob, P);
    status = SarStoreWriteAtomic(SAR_PRES_KEYTMP, SAR_PRES_KEYFILE, &blob, sizeof(blob),
                                 SAR_POOL_TAG_PRESBUF);
    RtlSecureZeroMemory(&blob, sizeof(blob));
    return status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static VOID SarPresPersistIndex(_Inout_ PSAR_PRESERVE P)
{
    ULONG64 c;
    size_t need;
    size_t out_len = 0;
    PUCHAR buf;
    UINT64 gen;
    int rc;

    FltAcquirePushLockExclusive(&P->lock);
    c = P->record_count;
    gen = P->generation + 1;
    need = sar_preserve_serialized_size(c);
    buf = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, need, SAR_POOL_TAG_PRESBUF);
    if (buf == NULL) {
        FltReleasePushLock(&P->lock);
        InterlockedExchange(&P->dirty, 1);
        return;
    }
    rc = sar_preserve_serialize(P->mac_key, P->records, c, gen, buf, need, &out_len);
    if (rc == SAR_PRES_OK) {
        sar_preserve_header_t hdr;
        RtlCopyMemory(&hdr, buf, sizeof(hdr));
        P->generation = gen;
        P->anchor.present = 1;
        P->anchor.generation = gen;
        RtlCopyMemory(P->anchor.head_mac, hdr.head_mac, SEMANTICS_AR_MAC_SIZE);
    }
    FltReleasePushLock(&P->lock);

    if (rc != SAR_PRES_OK) {
        ExFreePoolWithTag(buf, SAR_POOL_TAG_PRESBUF);
        InterlockedExchange(&P->dirty, 1);
        return;
    }

    if (!NT_SUCCESS(SarStoreWriteAtomic(SAR_PRES_INDEXTMP, SAR_PRES_INDEX, buf, (ULONG)out_len,
                                        SAR_POOL_TAG_PRESBUF)))
        InterlockedExchange(&P->dirty, 1);
    else
        SarPresWriteBlob(P);

    RtlSecureZeroMemory(buf, out_len);
    ExFreePoolWithTag(buf, SAR_POOL_TAG_PRESBUF);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static VOID SarPresMaintain(_Inout_ PSAR_PRESERVE P)
{
    LARGE_INTEGER now;
    uint64_t removed;

    KeQuerySystemTime(&now);
    FltAcquirePushLockExclusive(&P->lock);
    removed = sar_preserve_evict_aged(P->records, &P->record_count, (uint64_t)now.QuadPart,
                                      P->retention_100ns);
    if (removed > 0)
        P->free_dirty = TRUE;
    FltReleasePushLock(&P->lock);
    if (removed > 0)
        InterlockedExchange(&P->dirty, 1);
}

_Function_class_(KSTART_ROUTINE)
static VOID SarPresPersistThread(_In_ PVOID Context)
{
    PSAR_PRESERVE p = (PSAR_PRESERVE)Context;
    LARGE_INTEGER timeout;
    NTSTATUS w;

    for (;;) {
        timeout.QuadPart = SAR_PERSIST_DEBOUNCE_100NS;
        w = KeWaitForSingleObject(&p->stop, Executive, KernelMode, FALSE, &timeout);
        if (w == STATUS_SUCCESS)
            break;
        SarPresMaintain(p);
        if (InterlockedExchange(&p->dirty, 0))
            SarPresPersistIndex(p);
    }

    if (InterlockedExchange(&p->dirty, 0))
        SarPresPersistIndex(p);

    PsTerminateSystemThread(STATUS_SUCCESS);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarPreserveCreate(_In_ PFLT_FILTER Filter, _In_ PSAR_POSTURE Posture,
                           _Outptr_ PSAR_PRESERVE *Preserve)
{
    PSAR_PRESERVE p;
    NTSTATUS status;
    ULONG got = 0;

    *Preserve = NULL;

    p = (PSAR_PRESERVE)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*p), SAR_POOL_TAG_PRESCTX);
    if (p == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    p->filter = Filter;
    p->posture = Posture;
    p->record_capacity = SAR_PRESERVE_RECORD_CAPACITY;
    p->record_count = 0;
    p->free_count = 0;
    p->free_capacity = SAR_PRESERVE_RECORD_CAPACITY + 1;
    p->data_high_water = 0;
    p->free_dirty = FALSE;
    p->data_handle = NULL;
    p->aes_alg = NULL;
    p->key_obj_len = 0;
    p->have_key = FALSE;
    p->generation = 0;
    p->anchor.present = 0;
    p->anchor.generation = 0;
    RtlZeroMemory(p->anchor.head_mac, SEMANTICS_AR_MAC_SIZE);
    p->retention_100ns = SAR_PRESERVE_DEFAULT_RETENTION_100NS;
    p->capacity_bytes = SAR_PRESERVE_DEFAULT_CAPACITY_BYTES;
    p->ready = 0;
    p->dirty = 0;
    p->persist_thread = NULL;
    p->thread_started = FALSE;
    RtlZeroMemory(p->store_key, sizeof(p->store_key));
    RtlZeroMemory(p->mac_key, sizeof(p->mac_key));

    p->records = (sar_preserve_record_t *)ExAllocatePool2(
        POOL_FLAG_PAGED, (SIZE_T)p->record_capacity * sizeof(sar_preserve_record_t),
        SAR_POOL_TAG_PRESIDX);
    p->free_ext = (SAR_PRES_EXTENT *)ExAllocatePool2(
        POOL_FLAG_PAGED, (SIZE_T)p->free_capacity * sizeof(SAR_PRES_EXTENT),
        SAR_POOL_TAG_PRESFREE);
    if (p->records == NULL || p->free_ext == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto fail;
    }

    status = BCryptOpenAlgorithmProvider(&p->aes_alg, BCRYPT_AES_ALGORITHM, NULL, 0);
    if (!NT_SUCCESS(status))
        goto fail;
    status = BCryptSetProperty(p->aes_alg, BCRYPT_CHAINING_MODE,
                               (PUCHAR)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
    if (!NT_SUCCESS(status))
        goto fail;
    status = BCryptGetProperty(p->aes_alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&p->key_obj_len,
                               sizeof(p->key_obj_len), &got, 0);
    if (!NT_SUCCESS(status))
        goto fail;

    FltInitializePushLock(&p->lock);
    KeInitializeEvent(&p->stop, NotificationEvent, FALSE);

    *Preserve = p;
    return STATUS_SUCCESS;

fail:
    if (p->aes_alg != NULL)
        BCryptCloseAlgorithmProvider(p->aes_alg, 0);
    if (p->free_ext != NULL)
        ExFreePoolWithTag(p->free_ext, SAR_POOL_TAG_PRESFREE);
    if (p->records != NULL)
        ExFreePoolWithTag(p->records, SAR_POOL_TAG_PRESIDX);
    ExFreePoolWithTag(p, SAR_POOL_TAG_PRESCTX);
    return status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS SarPresOpenData(_Inout_ PSAR_PRESERVE P)
{
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    HANDLE h;
    NTSTATUS status;

    RtlInitUnicodeString(&name, SAR_PRES_DATA);
    InitializeObjectAttributes(&oa, &name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

    status = ZwCreateFile(&h, FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE, &oa, &iosb, NULL,
                          FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN_IF,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE |
                          FILE_WRITE_THROUGH, NULL, 0);
    if (!NT_SUCCESS(status))
        return status;

    P->data_handle = h;
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarPreserveLoad(_Inout_ PSAR_PRESERVE Preserve)
{
    NTSTATUS status;
    PUCHAR kbuf = NULL;
    SIZE_T klen = 0;
    PUCHAR fbuf = NULL;
    SIZE_T flen = 0;
    uint64_t count = 0;
    int vr;
    HANDLE th;
    OBJECT_ATTRIBUTES toa;

    SarStoreEnsureDir(SAR_PRES_DIR, SAR_POOL_TAG_PRESKEY);

    status = SarStoreReadAll(SAR_PRES_KEYFILE, SAR_POOL_TAG_PRESBUF, sizeof(SAR_PRESKEY_BLOB),
                             &kbuf, &klen);
    if (NT_SUCCESS(status) && kbuf != NULL && klen == sizeof(SAR_PRESKEY_BLOB)) {
        PSAR_PRESKEY_BLOB b = (PSAR_PRESKEY_BLOB)kbuf;
        if (b->magic == SAR_PRESKEY_MAGIC) {
            RtlCopyMemory(Preserve->store_key, b->store_key, SEMANTICS_AR_MAC_SIZE);
            RtlCopyMemory(Preserve->mac_key, b->mac_key, SEMANTICS_AR_MAC_SIZE);
            Preserve->anchor.present = b->anchor_present ? 1 : 0;
            Preserve->anchor.generation = b->anchor_generation;
            RtlCopyMemory(Preserve->anchor.head_mac, b->anchor_head_mac, SEMANTICS_AR_MAC_SIZE);
            if (b->capacity_bytes != 0) {
                Preserve->retention_100ns = b->retention_100ns;
                Preserve->capacity_bytes = b->capacity_bytes;
            }
            Preserve->have_key = TRUE;
        }
    }
    if (kbuf != NULL) {
        RtlSecureZeroMemory(kbuf, klen);
        ExFreePoolWithTag(kbuf, SAR_POOL_TAG_PRESBUF);
    }

    if (!Preserve->have_key) {
        if (!NT_SUCCESS(BCryptGenRandom(NULL, Preserve->store_key, SEMANTICS_AR_MAC_SIZE,
                                        BCRYPT_USE_SYSTEM_PREFERRED_RNG)) ||
            !NT_SUCCESS(BCryptGenRandom(NULL, Preserve->mac_key, SEMANTICS_AR_MAC_SIZE,
                                        BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
            return;
        Preserve->have_key = TRUE;
        SarPresWriteBlob(Preserve);
    }

    if (!NT_SUCCESS(SarPresOpenData(Preserve)))
        return;

    status = SarStoreReadAll(SAR_PRES_INDEX, SAR_POOL_TAG_PRESBUF, SAR_PRESERVE_MAX_INDEX_FILE,
                             &fbuf, &flen);
    if (NT_SUCCESS(status) && fbuf != NULL && flen >= sizeof(sar_preserve_header_t)) {
        vr = sar_preserve_verify(fbuf, flen, Preserve->mac_key,
                                 Preserve->anchor.present ? &Preserve->anchor : NULL, &count);
        if (vr == SAR_PRES_OK) {
            sar_preserve_header_t hdr;
            const UCHAR *p = fbuf + sizeof(hdr);
            uint64_t i;
            RtlCopyMemory(&hdr, fbuf, sizeof(hdr));
            if (count > Preserve->record_capacity)
                count = Preserve->record_capacity;
            for (i = 0; i < count; i++) {
                const sar_preserve_disk_record_t *d = (const sar_preserve_disk_record_t *)p;
                RtlCopyMemory(&Preserve->records[i], &d->fields, sizeof(d->fields));
                p += sizeof(*d);
            }
            Preserve->record_count = count;
            Preserve->generation = hdr.generation;
            Preserve->anchor.present = 1;
            Preserve->anchor.generation = hdr.generation;
            RtlCopyMemory(Preserve->anchor.head_mac, hdr.head_mac, SEMANTICS_AR_MAC_SIZE);
            Preserve->free_dirty = TRUE;
        } else {
            Preserve->record_count = 0;
        }
    }
    if (fbuf != NULL) {
        RtlSecureZeroMemory(fbuf, flen);
        ExFreePoolWithTag(fbuf, SAR_POOL_TAG_PRESBUF);
    }

    InterlockedExchange(&Preserve->ready, 1);

    InitializeObjectAttributes(&toa, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    if (NT_SUCCESS(PsCreateSystemThread(&th, THREAD_ALL_ACCESS, &toa, NULL, NULL,
                                        SarPresPersistThread, Preserve))) {
        if (NT_SUCCESS(ObReferenceObjectByHandle(th, THREAD_ALL_ACCESS, *PsThreadType,
                                                 KernelMode, (PVOID *)&Preserve->persist_thread,
                                                 NULL)))
            Preserve->thread_started = TRUE;
        ZwClose(th);
    }
}

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarPreserveReady(_In_opt_ PSAR_PRESERVE Preserve)
{
    return Preserve != NULL && Preserve->ready != 0;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarPreserveStage(_Inout_ PSAR_PRESERVE Preserve, _In_ const UINT16 *Path,
                          _In_ UINT64 Offset, _In_ UINT64 Length,
                          _In_reads_bytes_(PlaintextLen) const UCHAR *Plaintext,
                          _In_ ULONG PlaintextLen)
{
    UINT64 ct_len;
    UINT64 off;
    UINT64 target;
    UCHAR iv[SAR_PRESERVE_AES_BLOCK];
    PUCHAR ctbuf;
    ULONG produced = 0;
    LARGE_INTEGER now;
    sar_preserve_record_t rec;
    NTSTATUS status;

    if (Preserve == NULL || Preserve->ready == 0)
        return STATUS_DEVICE_NOT_READY;
    if (PlaintextLen == 0 || Length == 0)
        return STATUS_SUCCESS;

    ct_len = SarPresRound16(PlaintextLen);
    if (ct_len > Preserve->capacity_bytes)
        return STATUS_SUCCESS;

    if (!NT_SUCCESS(BCryptGenRandom(NULL, iv, SAR_PRESERVE_AES_BLOCK,
                                    BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
        return STATUS_UNSUCCESSFUL;

    ctbuf = (PUCHAR)ExAllocatePool2(POOL_FLAG_PAGED, (SIZE_T)ct_len, SAR_POOL_TAG_PRESBUF);
    if (ctbuf == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeQuerySystemTime(&now);

    FltAcquirePushLockExclusive(&Preserve->lock);

    if (sar_preserve_covered(Preserve->records, Preserve->record_count, Path, Offset, Length)) {
        FltReleasePushLock(&Preserve->lock);
        ExFreePoolWithTag(ctbuf, SAR_POOL_TAG_PRESBUF);
        return STATUS_SUCCESS;
    }

    if (sar_preserve_evict_aged(Preserve->records, &Preserve->record_count,
                                (uint64_t)now.QuadPart, Preserve->retention_100ns) > 0)
        Preserve->free_dirty = TRUE;

    target = Preserve->capacity_bytes - ct_len;
    if (sar_preserve_total_bytes(Preserve->records, Preserve->record_count) > target) {
        if (sar_preserve_evict_oldest(Preserve->records, &Preserve->record_count, target) > 0)
            Preserve->free_dirty = TRUE;
    }

    if (Preserve->record_count >= Preserve->record_capacity) {
        FltReleasePushLock(&Preserve->lock);
        ExFreePoolWithTag(ctbuf, SAR_POOL_TAG_PRESBUF);
        return STATUS_SUCCESS;
    }

    {
        PUCHAR padbuf = (PUCHAR)ExAllocatePool2(POOL_FLAG_PAGED, (SIZE_T)ct_len,
                                                SAR_POOL_TAG_PRESBUF);
        if (padbuf == NULL) {
            FltReleasePushLock(&Preserve->lock);
            ExFreePoolWithTag(ctbuf, SAR_POOL_TAG_PRESBUF);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(padbuf, (SIZE_T)ct_len);
        RtlCopyMemory(padbuf, Plaintext, PlaintextLen);
        status = SarPresCrypt(Preserve, TRUE, padbuf, (ULONG)ct_len, iv, ctbuf, (ULONG)ct_len,
                              &produced);
        RtlSecureZeroMemory(padbuf, (SIZE_T)ct_len);
        ExFreePoolWithTag(padbuf, SAR_POOL_TAG_PRESBUF);
    }

    if (!NT_SUCCESS(status)) {
        FltReleasePushLock(&Preserve->lock);
        ExFreePoolWithTag(ctbuf, SAR_POOL_TAG_PRESBUF);
        return status;
    }

    off = SarPresAlloc(Preserve, ct_len);

    status = SarPresDataWrite(Preserve, off, ctbuf, (ULONG)ct_len);
    RtlSecureZeroMemory(ctbuf, (SIZE_T)ct_len);
    ExFreePoolWithTag(ctbuf, SAR_POOL_TAG_PRESBUF);

    if (!NT_SUCCESS(status)) {
        Preserve->free_dirty = TRUE;
        FltReleasePushLock(&Preserve->lock);
        return status;
    }

    sar_preserve_record_init(&rec, Path, Offset, Length, (uint64_t)now.QuadPart, off, ct_len,
                             iv, SAR_PRESERVE_AES_BLOCK, Plaintext, PlaintextLen);
    if (sar_preserve_append(Preserve->records, &Preserve->record_count, Preserve->record_capacity,
                            &rec) == 1)
        InterlockedExchange(&Preserve->dirty, 1);

    FltReleasePushLock(&Preserve->lock);
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarPreserveReconcile(_Inout_ PSAR_PRESERVE Preserve, _In_ const UINT16 *Path,
                          _In_ UINT64 KeyOffset, _In_ UINT64 KeyLength)
{
    uint64_t removed;

    if (Preserve == NULL || Preserve->ready == 0)
        return;

    FltAcquirePushLockExclusive(&Preserve->lock);
    removed = sar_preserve_reconcile(Preserve->records, &Preserve->record_count, Path,
                                     KeyOffset, KeyLength);
    if (removed > 0)
        Preserve->free_dirty = TRUE;
    FltReleasePushLock(&Preserve->lock);

    if (removed > 0)
        InterlockedExchange(&Preserve->dirty, 1);
}

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarPreserveWouldExceed(_In_opt_ PSAR_PRESERVE Preserve, _In_ UINT64 IncomingBytes)
{
    BOOLEAN exceed;

    if (Preserve == NULL || Preserve->ready == 0)
        return FALSE;

    FltAcquirePushLockShared(&Preserve->lock);
    exceed = sar_preserve_total_bytes(Preserve->records, Preserve->record_count) + IncomingBytes >
             Preserve->capacity_bytes;
    FltReleasePushLock(&Preserve->lock);
    return exceed;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarPreserveRestore(_Inout_ PSAR_PRESERVE Preserve, _In_ const UINT16 *Path,
                            _In_ UINT64 Offset, _In_ UINT64 Length,
                            _Out_writes_bytes_to_(OutCap, *OutLen) PUCHAR Out,
                            _In_ ULONG OutCap, _Out_ PULONG OutLen)
{
    sar_preserve_record_t rec;
    BOOLEAN found = FALSE;
    uint64_t i;
    PUCHAR ctbuf;
    PUCHAR ptbuf;
    ULONG produced = 0;
    NTSTATUS status;

    *OutLen = 0;
    if (Preserve == NULL || Preserve->ready == 0)
        return STATUS_DEVICE_NOT_READY;
    if (Length > OutCap)
        return STATUS_BUFFER_TOO_SMALL;

    FltAcquirePushLockExclusive(&Preserve->lock);
    for (i = 0; i < Preserve->record_count; i++) {
        if (Preserve->records[i].provenance_offset == Offset &&
            Preserve->records[i].provenance_length == Length &&
            RtlEqualMemory(Preserve->records[i].provenance_path, Path,
                           SEMANTICS_AR_PROVENANCE_PATH_MAX * sizeof(UINT16))) {
            RtlCopyMemory(&rec, &Preserve->records[i], sizeof(rec));
            found = TRUE;
            break;
        }
    }
    if (!found) {
        FltReleasePushLock(&Preserve->lock);
        return STATUS_NOT_FOUND;
    }

    ctbuf = (PUCHAR)ExAllocatePool2(POOL_FLAG_PAGED, (SIZE_T)rec.payload_length,
                                    SAR_POOL_TAG_PRESBUF);
    ptbuf = (PUCHAR)ExAllocatePool2(POOL_FLAG_PAGED, (SIZE_T)rec.payload_length,
                                    SAR_POOL_TAG_PRESBUF);
    if (ctbuf == NULL || ptbuf == NULL) {
        if (ctbuf != NULL) ExFreePoolWithTag(ctbuf, SAR_POOL_TAG_PRESBUF);
        if (ptbuf != NULL) ExFreePoolWithTag(ptbuf, SAR_POOL_TAG_PRESBUF);
        FltReleasePushLock(&Preserve->lock);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = SarPresDataRead(Preserve, rec.payload_offset, ctbuf, (ULONG)rec.payload_length);
    if (NT_SUCCESS(status))
        status = SarPresCrypt(Preserve, FALSE, ctbuf, (ULONG)rec.payload_length, rec.iv, ptbuf,
                              (ULONG)rec.payload_length, &produced);

    if (NT_SUCCESS(status) &&
        sar_preserve_verify_restore(&rec, Path, Offset, ptbuf, Length) == SAR_PRES_OK) {
        RtlCopyMemory(Out, ptbuf, (SIZE_T)Length);
        *OutLen = (ULONG)Length;
    } else if (NT_SUCCESS(status)) {
        status = STATUS_DATA_ERROR;
    }

    RtlSecureZeroMemory(ptbuf, (SIZE_T)rec.payload_length);
    ExFreePoolWithTag(ctbuf, SAR_POOL_TAG_PRESBUF);
    ExFreePoolWithTag(ptbuf, SAR_POOL_TAG_PRESBUF);
    FltReleasePushLock(&Preserve->lock);
    return status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
int SarPreserveProject(_In_ PSAR_PRESERVE Preserve, _In_ ULONG64 Index,
                       _Out_ semantics_ar_preserve_entry_t *Entry, _Out_ ULONG64 *Total)
{
    int valid = 0;

    RtlZeroMemory(Entry, sizeof(*Entry));

    FltAcquirePushLockShared(&Preserve->lock);
    *Total = Preserve->record_count;
    if (Index < Preserve->record_count) {
        const sar_preserve_record_t *r = &Preserve->records[Index];
        RtlCopyMemory(Entry->provenance_path, r->provenance_path, sizeof(Entry->provenance_path));
        Entry->provenance_offset = r->provenance_offset;
        Entry->provenance_length = r->provenance_length;
        Entry->capture_time = r->capture_time;
        Entry->payload_length = r->payload_length;
        valid = 1;
    }
    FltReleasePushLock(&Preserve->lock);
    return valid;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarPreserveSetBudget(_Inout_ PSAR_PRESERVE Preserve, _In_ UINT64 Retention100ns,
                          _In_ UINT64 CapacityBytes)
{
    if (Preserve == NULL || Retention100ns == 0 || CapacityBytes == 0)
        return;

    FltAcquirePushLockExclusive(&Preserve->lock);
    Preserve->retention_100ns = Retention100ns;
    Preserve->capacity_bytes = CapacityBytes;
    FltReleasePushLock(&Preserve->lock);

    SarPresWriteBlob(Preserve);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarPreserveDestroy(_Inout_ PSAR_PRESERVE Preserve)
{
    if (Preserve == NULL)
        return;

    if (Preserve->thread_started) {
        KeSetEvent(&Preserve->stop, IO_NO_INCREMENT, FALSE);
        KeWaitForSingleObject(Preserve->persist_thread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Preserve->persist_thread);
    }

    if (Preserve->data_handle != NULL)
        ZwClose(Preserve->data_handle);

    if (Preserve->aes_alg != NULL)
        BCryptCloseAlgorithmProvider(Preserve->aes_alg, 0);

    FltDeletePushLock(&Preserve->lock);

    if (Preserve->records != NULL) {
        RtlSecureZeroMemory(Preserve->records,
                            (SIZE_T)(Preserve->record_capacity * sizeof(sar_preserve_record_t)));
        ExFreePoolWithTag(Preserve->records, SAR_POOL_TAG_PRESIDX);
    }
    if (Preserve->free_ext != NULL)
        ExFreePoolWithTag(Preserve->free_ext, SAR_POOL_TAG_PRESFREE);

    RtlSecureZeroMemory(Preserve->store_key, sizeof(Preserve->store_key));
    RtlSecureZeroMemory(Preserve->mac_key, sizeof(Preserve->mac_key));
    ExFreePoolWithTag(Preserve, SAR_POOL_TAG_PRESCTX);
}
