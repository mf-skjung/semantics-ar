#ifndef SEMANTICS_AR_DRIVER_H
#define SEMANTICS_AR_DRIVER_H

#include <fltKernel.h>
#include <ntddk.h>
#include <suppress.h>

#include "semantics_ar/protocol.h"
#include "sar_control.h"

#define SAR_POOL_TAG_STATE     'tSrS'
#define SAR_POOL_TAG_WHITELIST 'lWrS'
#define SAR_POOL_TAG_IDENTITY  'dIrS'
#define SAR_POOL_TAG_SEAM      'mSrS'
#define SAR_POOL_TAG_CAPBUF    'bCrS'
#define SAR_POOL_TAG_STREAMCTX 'cSrS'
#define SAR_POOL_TAG_COMM      'mCrS'
#define SAR_POOL_TAG_HANDSHAKE 'hHrS'
#define SAR_POOL_TAG_CAPCTX    'xCrS'
#define SAR_POOL_TAG_KEYSTORE  'sKrS'
#define SAR_POOL_TAG_BLOCKED   'kBrS'
#define SAR_POOL_TAG_GATEMAP   'gGrS'
#define SAR_POOL_TAG_CAPWORK   'wCrS'
#define SAR_POOL_TAG_CAPRES    'rCrS'
#define SAR_POOL_TAG_PERSIST   'pKrS'
#define SAR_POOL_TAG_KSBUF     'bKrS'
#define SAR_POOL_TAG_KSSEC     'eKrS'
#define SAR_POOL_TAG_RECOVER   'vRrS'
#define SAR_POOL_TAG_SNAPSHOT  'nSrS'
#define SAR_POOL_TAG_FLAGS     'lFrS'
#define SAR_POOL_TAG_PRESCTX   'xPrS'
#define SAR_POOL_TAG_PRESIDX   'iPrS'
#define SAR_POOL_TAG_PRESFREE  'fPrS'
#define SAR_POOL_TAG_PRESBUF   'bPrS'
#define SAR_POOL_TAG_PRESKEY   'kPrS'
#define SAR_POOL_TAG_PHANTOM  'hPrS'
#define SAR_POOL_TAG_SECTION  'cSeS'

#define SAR_WHITELIST_CAPACITY    256u
#define SAR_IDENTITY_BUCKET_COUNT 1024u
#define SAR_CAPTURE_BUFFER_BYTES  256u
#define SAR_KEYSTORE_CAPACITY     16384u
#define SAR_BLOCKED_CAPACITY      256u
#define SAR_CAPTURE_HEAP_BUDGET   (4u * 1024u * 1024u)
#define SAR_CAPTURE_HEAP_PER_CAP  (1u * 1024u * 1024u)
#define SAR_CAPTURE_HEAP_MAX      64u
#define SAR_CAPTURE_INFLIGHT_CAP  256
#define SAR_PERSIST_DEBOUNCE_100NS (-50000000LL)
#define SAR_SCANNED_CAPACITY       256u
#define SAR_PRESERVE_RECORD_CAPACITY        65536u
#define SAR_PRESERVE_MAX_INDEX_FILE         (256ull * 1024ull * 1024ull)
#define SAR_PRESERVE_DEFAULT_RETENTION_100NS (7ull * 24ull * 3600ull * 10000000ull)
#define SAR_PRESERVE_DEFAULT_CAPACITY_BYTES  (10ull * 1024ull * 1024ull * 1024ull)
#define SAR_PRESERVE_AES_BLOCK              16u
#define SAR_PRESERVE_STAGE_MAX              (4u * 1024u * 1024u)
#define SAR_PRESERVE_SECTOR                 4096u

typedef enum {
    SAR_DESTRUCT_NONE = 0,
    SAR_DESTRUCT_WRITE_CACHED = 1,
    SAR_DESTRUCT_WRITE_NONCACHED = 2,
    SAR_DESTRUCT_WRITE_PAGING = 3,
    SAR_DESTRUCT_SET_EOF = 4,
    SAR_DESTRUCT_SET_ALLOCATION = 5,
    SAR_DESTRUCT_DISPOSITION = 6,
    SAR_DESTRUCT_DISPOSITION_EX = 7,
    SAR_DESTRUCT_RENAME = 8,
    SAR_DESTRUCT_RENAME_EX = 9,
    SAR_DESTRUCT_LINK = 10,
    SAR_DESTRUCT_LINK_EX = 11,
    SAR_DESTRUCT_DUPLICATE_EXTENTS = 12,
    SAR_DESTRUCT_OFFLOAD_WRITE = 13,
    SAR_DESTRUCT_SET_ZERO_DATA = 14,
    SAR_DESTRUCT_FILE_LEVEL_TRIM = 15,
    SAR_DESTRUCT_SET_SPARSE = 16,
    SAR_DESTRUCT_LOCK_VOLUME = 17,
    SAR_DESTRUCT_DISMOUNT_VOLUME = 18,
    SAR_DESTRUCT_SECTION_SYNC = 19,
    SAR_DESTRUCT_MEMBER_COUNT = 20
} sar_destruct_member_t;

typedef enum {
    SAR_PPL_NONE = 0,
    SAR_PPL_AVAILABLE = 1,
    SAR_PPL_ANTIMALWARE = 2
} sar_ppl_class_t;

typedef struct _SAR_POSTURE {
    BOOLEAN hvci_active;
    BOOLEAN vbs_active;
    BOOLEAN tpm20_present;
    sar_ppl_class_t ppl_available;
    BOOLEAN dev_drive_supported;
    BOOLEAN dev_drive_attach_gap;
    BOOLEAN process_notify_ex2;
    BOOLEAN bypass_io_negotiated;
    BOOLEAN keystore_persistent;
    BOOLEAN keystore_tamper_detected;
    BOOLEAN preserve_active;
    BOOLEAN phantom_active;
} SAR_POSTURE, *PSAR_POSTURE;

struct _SAR_STATE;
struct _SAR_COMM;
struct _SAR_FEATURE_FNS;
struct _SAR_CAPTURE_CTX;
struct _SAR_KEYSTORE;
struct _SAR_PRESERVE;
struct _SAR_PHANTOM;

typedef struct _SAR_GLOBALS {
    PDRIVER_OBJECT driver_object;
    PFLT_FILTER filter;
    struct _SAR_STATE *state;
    struct _SAR_COMM *comm;
    struct _SAR_KEYSTORE *keystore;
    struct _SAR_CAPTURE_CTX *capture;
    struct _SAR_PRESERVE *preserve;
    struct _SAR_PHANTOM *phantom;
    SAR_POSTURE posture;
    BOOLEAN process_notify_registered;
} SAR_GLOBALS, *PSAR_GLOBALS;

extern SAR_GLOBALS g_sar;

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarFeatureDetect(_Inout_ PSAR_POSTURE Posture);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarProcessNotifyRegister(_Inout_ PSAR_POSTURE Posture);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarProcessNotifyUnregister(VOID);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarCommPortCreate(_In_ PFLT_FILTER Filter, _Outptr_ struct _SAR_COMM **Comm);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarCommPortClose(_Inout_ struct _SAR_COMM *Comm);

#endif
