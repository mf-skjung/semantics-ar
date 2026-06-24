#ifndef SEMANTICS_AR_DRIVER_KEYSTORE_PERSIST_H
#define SEMANTICS_AR_DRIVER_KEYSTORE_PERSIST_H

#include <fltKernel.h>

#include "driver.h"
#include "semantics_ar/keystore_format.h"

#define SAR_MACKEY_MAGIC   0x314B4D53u
#define SAR_MACKEY_VERSION 1u

#pragma pack(push, 1)
typedef struct _SAR_MACKEY_BLOB {
    UINT32 magic;
    UINT32 version;
    UCHAR  mac_key[SEMANTICS_AR_MAC_SIZE];
    UCHAR  anchor_present;
    UCHAR  reserved[7];
    UINT64 anchor_generation;
    UCHAR  anchor_head_mac[SEMANTICS_AR_MAC_SIZE];
} SAR_MACKEY_BLOB, *PSAR_MACKEY_BLOB;
#pragma pack(pop)

typedef struct _SAR_KEYSTORE SAR_KEYSTORE, *PSAR_KEYSTORE;

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarKeystoreCreate(_In_ PFLT_FILTER Filter, _In_ PSAR_POSTURE Posture,
                           _Outptr_ PSAR_KEYSTORE *Keystore);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarKeystoreLoad(_Inout_ PSAR_KEYSTORE Keystore);

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarKeystoreReady(_In_opt_ PSAR_KEYSTORE Keystore);

_IRQL_requires_max_(PASSIVE_LEVEL)
const UCHAR *SarKeystoreMacKey(_In_ PSAR_KEYSTORE Keystore);

_IRQL_requires_max_(PASSIVE_LEVEL)
int SarKeystoreAppend(_Inout_ PSAR_KEYSTORE Keystore,
                      _In_ const semantics_ar_keystore_record_t *Record);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarKeystoreFlush(_Inout_ PSAR_KEYSTORE Keystore);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarKeystoreDestroy(_Inout_ PSAR_KEYSTORE Keystore);

#endif
