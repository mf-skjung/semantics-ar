#include "keystore_persist.h"
#include "state.h"
#include "store_io.h"

#include <ntifs.h>
#include <bcrypt.h>

#include "sar_keystore.h"
#include "sar_keystore_mgr.h"

extern PSAR_STATE g_sar_state;

#define SAR_KS_DIR     L"\\SystemRoot\\System32\\drivers\\SemanticsAr"
#define SAR_KS_FILE    L"\\SystemRoot\\System32\\drivers\\SemanticsAr\\keystore.bin"
#define SAR_KS_TMP     L"\\SystemRoot\\System32\\drivers\\SemanticsAr\\keystore.tmp"
#define SAR_KS_KEYFILE L"\\SystemRoot\\System32\\drivers\\SemanticsAr\\mackey.bin"
#define SAR_KS_KEYTMP  L"\\SystemRoot\\System32\\drivers\\SemanticsAr\\mackey.tmp"
#define SAR_KS_MAX_FILE (64ull * 1024ull * 1024ull)

struct _SAR_KEYSTORE {
    PFLT_FILTER filter;
    PSAR_POSTURE posture;

    EX_PUSH_LOCK lock;
    semantics_ar_keystore_record_t *records;
    ULONG64 record_count;
    ULONG64 record_capacity;

    UCHAR mac_key[SEMANTICS_AR_MAC_SIZE];
    BOOLEAN have_key;
    UINT64 generation;
    sar_keystore_anchor_t anchor;

    volatile LONG ready;
    volatile LONG dirty;

    KEVENT stop;
    PETHREAD persist_thread;
    BOOLEAN thread_started;
};

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS SarKsGenMacKey(_Out_writes_bytes_(SEMANTICS_AR_MAC_SIZE) PUCHAR Key)
{
    return BCryptGenRandom(NULL, Key, SEMANTICS_AR_MAC_SIZE, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static VOID SarKsFillBlob(_Out_ PSAR_MACKEY_BLOB Blob, _In_ const UCHAR *MacKey,
                         _In_ const sar_keystore_anchor_t *Anchor)
{
    RtlZeroMemory(Blob, sizeof(*Blob));
    Blob->magic = SAR_MACKEY_MAGIC;
    Blob->version = SAR_MACKEY_VERSION;
    RtlCopyMemory(Blob->mac_key, MacKey, SEMANTICS_AR_MAC_SIZE);
    Blob->anchor_present = (UCHAR)(Anchor->present ? 1 : 0);
    Blob->anchor_generation = Anchor->generation;
    RtlCopyMemory(Blob->anchor_head_mac, Anchor->head_mac, SEMANTICS_AR_MAC_SIZE);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static VOID SarKsPersist(_Inout_ PSAR_KEYSTORE Ks)
{
    ULONG64 c;
    size_t need;
    size_t out_len = 0;
    PUCHAR buf;
    sar_keystore_anchor_t na;
    sar_ksm_status_t s;
    SAR_MACKEY_BLOB blob;

    FltAcquirePushLockShared(&Ks->lock);
    c = Ks->record_count;
    FltReleasePushLock(&Ks->lock);

    need = sar_keystore_serialized_size(c);
    buf = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, need, SAR_POOL_TAG_KSBUF);
    if (buf == NULL) {
        InterlockedExchange(&Ks->dirty, 1);
        return;
    }

    FltAcquirePushLockShared(&Ks->lock);
    s = sar_ksm_persist(Ks->mac_key, Ks->records, c, Ks->generation, buf, need, &out_len, &na);
    FltReleasePushLock(&Ks->lock);

    if (s != SAR_KSM_OK) {
        ExFreePoolWithTag(buf, SAR_POOL_TAG_KSBUF);
        InterlockedExchange(&Ks->dirty, 1);
        return;
    }

    if (!NT_SUCCESS(SarStoreWriteAtomic(SAR_KS_TMP, SAR_KS_FILE, buf, (ULONG)out_len,
                                        SAR_POOL_TAG_KSBUF))) {
        ExFreePoolWithTag(buf, SAR_POOL_TAG_KSBUF);
        InterlockedExchange(&Ks->dirty, 1);
        return;
    }
    ExFreePoolWithTag(buf, SAR_POOL_TAG_KSBUF);

    Ks->generation = na.generation;
    Ks->anchor = na;

    SarKsFillBlob(&blob, Ks->mac_key, &na);
    if (!NT_SUCCESS(SarStoreWriteAtomic(SAR_KS_KEYTMP, SAR_KS_KEYFILE, &blob, sizeof(blob), SAR_POOL_TAG_KSBUF)))
        InterlockedExchange(&Ks->dirty, 1);
    RtlSecureZeroMemory(&blob, sizeof(blob));

    FltAcquirePushLockShared(&Ks->lock);
    c = Ks->record_count > c ? 1 : 0;
    FltReleasePushLock(&Ks->lock);
    if (c)
        InterlockedExchange(&Ks->dirty, 1);
}

_Function_class_(KSTART_ROUTINE)
static VOID SarKsPersistThread(_In_ PVOID Context)
{
    PSAR_KEYSTORE ks = (PSAR_KEYSTORE)Context;
    LARGE_INTEGER timeout;
    NTSTATUS w;

    for (;;) {
        timeout.QuadPart = SAR_PERSIST_DEBOUNCE_100NS;
        w = KeWaitForSingleObject(&ks->stop, Executive, KernelMode, FALSE, &timeout);
        if (w == STATUS_SUCCESS)
            break;
        if (InterlockedExchange(&ks->dirty, 0))
            SarKsPersist(ks);
    }

    if (InterlockedExchange(&ks->dirty, 0))
        SarKsPersist(ks);

    PsTerminateSystemThread(STATUS_SUCCESS);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarKeystoreCreate(_In_ PFLT_FILTER Filter, _In_ PSAR_POSTURE Posture,
                           _Outptr_ PSAR_KEYSTORE *Keystore)
{
    PSAR_KEYSTORE ks;

    *Keystore = NULL;

    ks = (PSAR_KEYSTORE)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*ks), SAR_POOL_TAG_PERSIST);
    if (ks == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    ks->filter = Filter;
    ks->posture = Posture;
    ks->record_capacity = SAR_KEYSTORE_CAPACITY;
    ks->record_count = 0;
    ks->have_key = FALSE;
    ks->generation = 0;
    ks->anchor.present = 0;
    ks->anchor.generation = 0;
    RtlZeroMemory(ks->anchor.head_mac, SEMANTICS_AR_MAC_SIZE);
    ks->ready = 0;
    ks->dirty = 0;
    ks->persist_thread = NULL;
    ks->thread_started = FALSE;
    RtlZeroMemory(ks->mac_key, sizeof(ks->mac_key));

    ks->records = (semantics_ar_keystore_record_t *)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        SAR_KEYSTORE_CAPACITY * sizeof(semantics_ar_keystore_record_t),
        SAR_POOL_TAG_KEYSTORE);
    if (ks->records == NULL) {
        ExFreePoolWithTag(ks, SAR_POOL_TAG_PERSIST);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    FltInitializePushLock(&ks->lock);
    KeInitializeEvent(&ks->stop, NotificationEvent, FALSE);

    *Keystore = ks;
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarKeystoreLoad(_Inout_ PSAR_KEYSTORE Ks)
{
    NTSTATUS status;
    PUCHAR kbuf = NULL;
    SIZE_T klen = 0;
    PUCHAR fbuf = NULL;
    SIZE_T flen = 0;
    BOOLEAN persist_ok = TRUE;
    SAR_MACKEY_BLOB blob;
    uint64_t count = 0;
    sar_keystore_anchor_t aout;
    sar_ksm_load_result_t res;
    sar_ksm_status_t ls;
    HANDLE th;
    OBJECT_ATTRIBUTES toa;

    SarStoreEnsureDir(SAR_KS_DIR, SAR_POOL_TAG_KSSEC);

    status = SarStoreReadAll(SAR_KS_KEYFILE, SAR_POOL_TAG_KSBUF, SAR_KS_MAX_FILE, &kbuf, &klen);
    if (NT_SUCCESS(status) && kbuf != NULL && klen == sizeof(SAR_MACKEY_BLOB)) {
        PSAR_MACKEY_BLOB b = (PSAR_MACKEY_BLOB)kbuf;
        if (b->magic == SAR_MACKEY_MAGIC && b->version == SAR_MACKEY_VERSION) {
            RtlCopyMemory(Ks->mac_key, b->mac_key, SEMANTICS_AR_MAC_SIZE);
            Ks->anchor.present = b->anchor_present ? 1 : 0;
            Ks->anchor.generation = b->anchor_generation;
            RtlCopyMemory(Ks->anchor.head_mac, b->anchor_head_mac, SEMANTICS_AR_MAC_SIZE);
            Ks->have_key = TRUE;
        }
    }
    if (kbuf != NULL) {
        RtlSecureZeroMemory(kbuf, klen);
        ExFreePoolWithTag(kbuf, SAR_POOL_TAG_KSBUF);
    }

    if (!Ks->have_key) {
        if (!NT_SUCCESS(SarKsGenMacKey(Ks->mac_key))) {
            Ks->posture->keystore_persistent = FALSE;
            return;
        }
        Ks->anchor.present = 0;
        Ks->anchor.generation = 0;
        RtlZeroMemory(Ks->anchor.head_mac, SEMANTICS_AR_MAC_SIZE);
        Ks->have_key = TRUE;

        SarKsFillBlob(&blob, Ks->mac_key, &Ks->anchor);
        if (!NT_SUCCESS(SarStoreWriteAtomic(SAR_KS_KEYTMP, SAR_KS_KEYFILE, &blob, sizeof(blob), SAR_POOL_TAG_KSBUF)))
            persist_ok = FALSE;
        RtlSecureZeroMemory(&blob, sizeof(blob));
    }

    status = SarStoreReadAll(SAR_KS_FILE, SAR_POOL_TAG_KSBUF, SAR_KS_MAX_FILE, &fbuf, &flen);
    if (status == STATUS_OBJECT_NAME_NOT_FOUND || status == STATUS_OBJECT_PATH_NOT_FOUND) {
        fbuf = NULL;
        flen = 0;
    } else if (!NT_SUCCESS(status)) {
        fbuf = NULL;
        flen = 0;
        persist_ok = FALSE;
    }

    ls = sar_ksm_load(fbuf, flen, Ks->mac_key,
                      Ks->anchor.present ? &Ks->anchor : NULL,
                      Ks->records, Ks->record_capacity, &count, &aout, &res);
    if (ls == SAR_KSM_OK) {
        Ks->record_count = count;
        Ks->generation = res.generation;
        Ks->anchor = aout;
        if (res.anchor_advanced && persist_ok) {
            SarKsFillBlob(&blob, Ks->mac_key, &aout);
            SarStoreWriteAtomic(SAR_KS_KEYTMP, SAR_KS_KEYFILE, &blob, sizeof(blob), SAR_POOL_TAG_KSBUF);
            RtlSecureZeroMemory(&blob, sizeof(blob));
        }
    } else if (ls == SAR_KSM_EMPTY) {
        Ks->record_count = 0;
        Ks->generation = Ks->anchor.present ? Ks->anchor.generation : 0;
    } else {
        Ks->posture->keystore_tamper_detected = TRUE;
        Ks->record_count = 0;
        Ks->generation = Ks->anchor.present ? Ks->anchor.generation : 0;
    }

    if (fbuf != NULL) {
        RtlSecureZeroMemory(fbuf, flen);
        ExFreePoolWithTag(fbuf, SAR_POOL_TAG_KSBUF);
    }

    Ks->posture->keystore_persistent = persist_ok;
    InterlockedExchange(&Ks->ready, 1);

    if (persist_ok) {
        InitializeObjectAttributes(&toa, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
        status = PsCreateSystemThread(&th, THREAD_ALL_ACCESS, &toa, NULL, NULL,
                                      SarKsPersistThread, Ks);
        if (NT_SUCCESS(status)) {
            if (NT_SUCCESS(ObReferenceObjectByHandle(th, THREAD_ALL_ACCESS, *PsThreadType,
                                                     KernelMode, (PVOID *)&Ks->persist_thread, NULL)))
                Ks->thread_started = TRUE;
            ZwClose(th);
        } else {
            Ks->posture->keystore_persistent = FALSE;
        }
    }
}

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarKeystoreReady(_In_opt_ PSAR_KEYSTORE Keystore)
{
    return Keystore != NULL && Keystore->ready != 0;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
const UCHAR *SarKeystoreMacKey(_In_ PSAR_KEYSTORE Keystore)
{
    return Keystore->mac_key;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
int SarKeystoreAppend(_Inout_ PSAR_KEYSTORE Keystore,
                      _In_ const semantics_ar_keystore_record_t *Record)
{
    int rc;

    FltAcquirePushLockExclusive(&Keystore->lock);
    rc = sar_keystore_append(Keystore->records, &Keystore->record_count,
                             Keystore->record_capacity, Record);
    FltReleasePushLock(&Keystore->lock);

    if (rc == 1) {
        InterlockedExchange(&Keystore->dirty, 1);
        InterlockedIncrement64(&g_sar_state->captured_key_count);
    }
    return rc;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
int SarKeystoreProject(_In_ PSAR_KEYSTORE Keystore, _In_ ULONG64 Index,
                       _Out_ semantics_ar_catalog_entry_t *Entry, _Out_ ULONG64 *Total)
{
    int valid = 0;

    RtlZeroMemory(Entry, sizeof(*Entry));

    FltAcquirePushLockShared(&Keystore->lock);
    *Total = Keystore->record_count;
    if (Index < Keystore->record_count) {
        const semantics_ar_keystore_record_t *r = &Keystore->records[Index];
        RtlCopyMemory(Entry->key_id, r->key_id, SEMANTICS_AR_KEY_ID_SIZE);
        Entry->algorithm = r->algorithm;
        Entry->mode = r->mode;
        Entry->provenance_offset = r->provenance_offset;
        RtlCopyMemory(Entry->provenance_path, r->provenance_path,
                      sizeof(Entry->provenance_path));
        Entry->capture_time = r->capture_time;
        Entry->actor_start_key = r->actor_start_key;
        valid = 1;
    }
    FltReleasePushLock(&Keystore->lock);
    return valid;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
int SarKeystoreRecordAt(_In_ PSAR_KEYSTORE Keystore, _In_ ULONG64 Index,
                        _Out_ semantics_ar_keystore_record_t *Record, _Out_ ULONG64 *Total)
{
    int valid = 0;

    FltAcquirePushLockShared(&Keystore->lock);
    *Total = Keystore->record_count;
    if (Index < Keystore->record_count) {
        RtlCopyMemory(Record, &Keystore->records[Index], sizeof(*Record));
        valid = 1;
    }
    FltReleasePushLock(&Keystore->lock);
    return valid;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarKeystoreDestroy(_Inout_ PSAR_KEYSTORE Keystore)
{
    if (Keystore == NULL)
        return;

    if (Keystore->thread_started) {
        KeSetEvent(&Keystore->stop, IO_NO_INCREMENT, FALSE);
        KeWaitForSingleObject(Keystore->persist_thread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Keystore->persist_thread);
    }

    FltDeletePushLock(&Keystore->lock);

    if (Keystore->records != NULL) {
        RtlSecureZeroMemory(Keystore->records,
                            (SIZE_T)(Keystore->record_capacity *
                                     sizeof(semantics_ar_keystore_record_t)));
        ExFreePoolWithTag(Keystore->records, SAR_POOL_TAG_KEYSTORE);
    }

    RtlSecureZeroMemory(Keystore->mac_key, sizeof(Keystore->mac_key));
    ExFreePoolWithTag(Keystore, SAR_POOL_TAG_PERSIST);
}
