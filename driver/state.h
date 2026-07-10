#ifndef SEMANTICS_AR_DRIVER_STATE_H
#define SEMANTICS_AR_DRIVER_STATE_H

#include <fltKernel.h>

#include "sar_control.h"

typedef struct _SAR_IDENTITY_ENTRY {
    LIST_ENTRY link;
    HANDLE process_id;
    PEPROCESS process;
    UINT64 start_key;
    sar_id_state_t id_state;
    sar_identity_t identity;
    BOOLEAN identity_valid;
    BOOLEAN subsystem_process;
    BOOLEAN protected_trusted;
    volatile LONG phantom_evidence;
} SAR_IDENTITY_ENTRY, *PSAR_IDENTITY_ENTRY;

typedef struct _SAR_STATE {
    volatile LONG mode;
    EX_PUSH_LOCK whitelist_lock;
    sar_whitelist_t whitelist;
    sar_identity_t *whitelist_storage;
    EX_PUSH_LOCK identity_lock;
    LIST_ENTRY *identity_buckets;
    ULONG identity_bucket_count;
    volatile LONG64 captured_key_count;
} SAR_STATE, *PSAR_STATE;

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarStateCreate(_Outptr_ PSAR_STATE *State);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarStateDestroy(_Inout_ PSAR_STATE State);

_IRQL_requires_max_(APC_LEVEL)
ULONG SarStateModeGet(_In_ PSAR_STATE State);

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarStateModeSet(_Inout_ PSAR_STATE State, _In_ ULONG RequestedMode);

_IRQL_requires_max_(APC_LEVEL)
sar_wl_status_t SarStateWhitelistAdd(_Inout_ PSAR_STATE State, _In_ const sar_identity_t *Identity);

_IRQL_requires_max_(APC_LEVEL)
sar_wl_status_t SarStateWhitelistRemove(_Inout_ PSAR_STATE State, _In_ const sar_identity_t *Identity);

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarStateWhitelistMatch(_In_ PSAR_STATE State, _In_ const sar_identity_t *Identity);

_IRQL_requires_max_(APC_LEVEL)
NTSTATUS SarStateIdentityInsert(_Inout_ PSAR_STATE State,
                                _In_ HANDLE ProcessId,
                                _In_ PEPROCESS Process,
                                _In_opt_ const sar_identity_t *Identity,
                                _In_ BOOLEAN IdentityValid,
                                _In_ BOOLEAN SubsystemProcess,
                                _In_ BOOLEAN ProtectedTrusted);

_IRQL_requires_max_(APC_LEVEL)
VOID SarStateIdentityRemove(_Inout_ PSAR_STATE State, _In_ HANDLE ProcessId);

_IRQL_requires_max_(APC_LEVEL)
sar_id_state_t SarStateIdentityLookup(_In_ PSAR_STATE State, _In_ HANDLE ProcessId);

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarStateIdentityQuery(_In_ PSAR_STATE State, _In_ HANDLE ProcessId,
                             _Out_ UINT64 *StartKey, _Out_ sar_id_state_t *IdState);

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarStateImageByStartKey(_In_ PSAR_STATE State, _In_ UINT64 StartKey,
                                _Out_writes_(SEMANTICS_AR_PROTO_PATH_MAX) PUINT16 OutPath);

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarStateIdentityApplyVerdict(_Inout_ PSAR_STATE State,
                                  _In_ HANDLE ProcessId,
                                  _In_ UINT64 StartKey,
                                  _In_ const sar_identity_t *VerifiedIdentity);

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarStateProtectedTarget(_In_ PSAR_STATE State, _In_ PEPROCESS Target,
                                _Out_opt_ PUINT64 StartKey);

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarStateOpenerTrusted(_In_ PSAR_STATE State, _In_ PEPROCESS Opener);

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarStateRevokeExemption(_Inout_ PSAR_STATE State, _In_ HANDLE ProcessId,
                               _Out_opt_ PUINT64 StartKey);

#endif
