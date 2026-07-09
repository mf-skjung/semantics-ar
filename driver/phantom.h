#ifndef SEMANTICS_AR_DRIVER_PHANTOM_H
#define SEMANTICS_AR_DRIVER_PHANTOM_H

#include <fltKernel.h>
#include "driver.h"

#define SAR_PHANTOM_K_THRESHOLD      3
#define SAR_PHANTOM_MAX_PER_DIR      3
#define SAR_PHANTOM_SECRET_BYTES     32
#define SAR_PHANTOM_NAME_CHARS       32
#define SAR_PHANTOM_MIN_SIZE         (4u * 1024u)
#define SAR_PHANTOM_MAX_SIZE         (16u * 1024u * 1024u)
#define SAR_PHANTOM_FRN_BASE         0x0000400000000000ULL
#define SAR_PHANTOM_FRN_SPAN         0x0000010000000000ULL
#define SAR_PHANTOM_FRN_INDEX_MASK   0x0000FFFFFFFFFFFFULL
#define SAR_PHANTOM_BACKING_SUBDIR   L"\\phantom"

#define SAR_PHANTOM_DIR      L"\\SystemRoot\\System32\\drivers\\SemanticsAr\\phantom"

typedef struct _SAR_DIR_OFFSETS {
    ULONG next_entry;
    ULONG file_index;
    ULONG creation_time;
    ULONG last_access;
    ULONG last_write;
    ULONG change_time;
    ULONG end_of_file;
    ULONG alloc_size;
    ULONG attrs;
    ULONG name_length;
    ULONG name_start;
    LONG  ea_size;
    LONG  short_name_length;
    LONG  short_name;
    LONG  file_id;
    LONG  file_id_128;
    ULONG base_record_size;
} SAR_DIR_OFFSETS;

typedef struct _SAR_PHANTOM_ENUM_CONTEXT {
    BOOLEAN emitted;
    BOOLEAN pattern_captured;
    ULONG   real_seen;
    USHORT  pattern_bytes;
    WCHAR   pattern[260];
} SAR_PHANTOM_ENUM_CONTEXT, *PSAR_PHANTOM_ENUM_CONTEXT;

typedef struct _SAR_PHANTOM {
    PFLT_FILTER filter;
    BOOLEAN active;
    UINT8 volume_secret[SAR_PHANTOM_SECRET_BYTES];
    BOOLEAN image_notify_registered;
} SAR_PHANTOM, *PSAR_PHANTOM;

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarPhantomCreate(_In_ PFLT_FILTER Filter, _Outptr_ PSAR_PHANTOM *Phantom);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarPhantomActivate(_Inout_ PSAR_PHANTOM Phantom, _In_ const UCHAR MacKey[32]);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarPhantomDestroy(_Inout_ PSAR_PHANTOM Phantom);

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarPhantomActive(_In_opt_ PSAR_PHANTOM Phantom);

FLT_PREOP_CALLBACK_STATUS
SarPhantomPreCreate(_Inout_ PFLT_CALLBACK_DATA Data,
                    _In_ PCFLT_RELATED_OBJECTS FltObjects,
                    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext);

FLT_PREOP_CALLBACK_STATUS
SarPhantomPreDirControl(_Inout_ PFLT_CALLBACK_DATA Data,
                        _In_ PCFLT_RELATED_OBJECTS FltObjects,
                        _Flt_CompletionContext_Outptr_ PVOID *CompletionContext);

FLT_POSTOP_CALLBACK_STATUS
SarPhantomPostDirControl(_Inout_ PFLT_CALLBACK_DATA Data,
                         _In_ PCFLT_RELATED_OBJECTS FltObjects,
                         _In_opt_ PVOID CompletionContext,
                         _In_ FLT_POST_OPERATION_FLAGS Flags);

FLT_POSTOP_CALLBACK_STATUS
SarPhantomPostQueryInfo(_Inout_ PFLT_CALLBACK_DATA Data,
                        _In_ PCFLT_RELATED_OBJECTS FltObjects,
                        _In_opt_ PVOID CompletionContext,
                        _In_ FLT_POST_OPERATION_FLAGS Flags);

FLT_POSTOP_CALLBACK_STATUS
SarPhantomPostFsControl(_Inout_ PFLT_CALLBACK_DATA Data,
                        _In_ PCFLT_RELATED_OBJECTS FltObjects,
                        _In_opt_ PVOID CompletionContext,
                        _In_ FLT_POST_OPERATION_FLAGS Flags);

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarPhantomIsPhantomPath(_In_ PCUNICODE_STRING Path);

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarPhantomIsTrustedProcess(_In_ HANDLE ProcessId);

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarPhantomEvidenceConvicts(_In_ HANDLE ProcessId);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarPhantomImageNotifyRegister(VOID);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarPhantomImageNotifyUnregister(VOID);

#endif
