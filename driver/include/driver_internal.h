#ifndef SEMANTICS_AR_DRIVER_INTERNAL_H
#define SEMANTICS_AR_DRIVER_INTERNAL_H

#define POOL_ZERO_DOWN_LEVEL_SUPPORT
#define POOL_NX_OPTIN

#include <fltKernel.h>
#include <bcrypt.h>
#include "semantics_ar/protocol.h"
#include "semantics_ar/errors.h"
#include "semantics_ar/shadow_format.h"

#define SEMANTICS_AR_POOL_TAG      'rAmS'
#define SEMANTICS_AR_POOL_TAG_PG   'pAmS'
#define SEMANTICS_AR_POOL_TAG_AL   'aAmS'
#define SEMANTICS_AR_PORT_NAME     L"\\SemanticsArPort"

#define SEMANTICS_AR_DEFAULT_REPLY_TIMEOUT_MS 5000

#define SEMANTICS_AR_CTX_HAS_WRITABLE_SECTION 0x00000001
#define SEMANTICS_AR_CTX_CAPTURE_QUEUED       0x00000002

#define SEMANTICS_AR_CHUNK_SIZE        4096
#define SEMANTICS_AR_MIN_INSPECT_SIZE  SEMANTICS_AR_CHUNK_SIZE
#define SEMANTICS_AR_DEFAULT_SECTOR    4096

#define SEMANTICS_AR_TRUNCATION_MAX_BYTES (4 * 1024 * 1024)

#define SEMANTICS_AR_PROC_INACTIVE   0
#define SEMANTICS_AR_PROC_TRACKED    1
#define SEMANTICS_AR_PROC_CONFIRMED  2

#define SEMANTICS_AR_MONITOR_FLAG_PROCESS_EXITED 0x01

#define SEMANTICS_AR_MAX_PROCESS_MONITORS      256
#define SEMANTICS_AR_CREATED_FILE_RING_SIZE    16384
#define SEMANTICS_AR_CREATE_RING_SIZE          65536

#define SEMANTICS_AR_MAC_KEY_SIZE     32
#define SEMANTICS_AR_SHADOW_DIR_NAME L"$SemanticsAR_Shadow"

#define SEMANTICS_AR_JOURNAL_DRAIN_TIMEOUT_100NS (-50000000LL)
#define SEMANTICS_AR_JOURNAL_DRAIN_RECHECK_100NS (-10000000LL)

#ifndef FileDispositionInformationEx
#define FileDispositionInformationEx ((FILE_INFORMATION_CLASS)64)
#endif
#ifndef FILE_DISPOSITION_FLAG_DELETE
#define FILE_DISPOSITION_FLAG_DELETE 0x00000001
#endif
#ifndef FileRenameInformationEx
#define FileRenameInformationEx ((FILE_INFORMATION_CLASS)65)
#endif
#ifndef FileLinkInformationEx
#define FileLinkInformationEx ((FILE_INFORMATION_CLASS)72)
#endif

#ifndef FSCTL_SET_ZERO_DATA
#define FSCTL_SET_ZERO_DATA CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 50, METHOD_BUFFERED, FILE_WRITE_DATA)
typedef struct _FILE_ZERO_DATA_INFORMATION {
    LARGE_INTEGER FileOffset;
    LARGE_INTEGER BeyondFinalZero;
} FILE_ZERO_DATA_INFORMATION, *PFILE_ZERO_DATA_INFORMATION;
#endif

#ifndef FSCTL_FILE_LEVEL_TRIM
#define FSCTL_FILE_LEVEL_TRIM CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 130, METHOD_BUFFERED, FILE_WRITE_DATA)
#endif

typedef enum {
    SEMANTICS_AR_TGATE_SKIP = 0,
    SEMANTICS_AR_TGATE_PRESERVE = 1
} semantics_ar_tgate_verdict_t;

typedef struct _semantics_ar_stream_context_t {
    volatile LONG  Flags;
} semantics_ar_stream_context_t;

typedef struct _semantics_ar_instance_context_t {
    UNICODE_STRING    ShadowBasePath;
    WCHAR             ShadowBasePathBuffer[320];
    volatile LONG     ShadowInitialized;
    HANDLE            JournalHandle;
    PFILE_OBJECT      JournalFileObject;
    ERESOURCE         JournalResource;
    BOOLEAN           JournalResourceInitialized;
    LARGE_INTEGER     JournalWriteOffset;
    volatile LONG     AcceptNewJournalWrites;
    ULONG             SectorSize;
    ULONG             ClusterSize;
    volatile LONGLONG ShadowBytesUsed;
    LONGLONG          ShadowMaxBytes;
    volatile LONG     PendingJournalWrites;
    KEVENT            JournalDrainEvent;
} semantics_ar_instance_context_t;

typedef struct _semantics_ar_trusted_pid_slot_t {
    volatile ULONG Pid;
    LONGLONG       CreationTime;
} semantics_ar_trusted_pid_slot_t;

typedef struct _semantics_ar_create_ring_entry_t {
    UINT64        FileId;
    LARGE_INTEGER Timestamp;
    volatile ULONG Pid;
} semantics_ar_create_ring_entry_t;

typedef struct _semantics_ar_process_monitor_t {
    volatile ULONG    Pid;
    volatile ULONG    State;
    ULONG             ParentPid;
    LARGE_INTEGER     FirstDetectionTime;
    ULONG             Flags;
    PUINT64           CreatedFileRing;
    ULONG             CreatedFileRingCursor;
    ULONG             CreatedFileRingCount;
    volatile LONGLONG ShadowBytesUsed;
} semantics_ar_process_monitor_t;

typedef struct _semantics_ar_global_data_t {
    PFLT_FILTER FilterHandle;
    PFLT_PORT   ServerPort;
    PFLT_PORT   ClientPort;

    semantics_ar_config_t Config;
    FAST_MUTEX            ConfigLock;

    volatile LONG ProtectionActive;
    ULONG   ServiceProcessId;
    LONGLONG ServiceCreationTime;

    semantics_ar_trusted_pid_slot_t TrustedPids[SEMANTICS_AR_MAX_TRUSTED_PIDS];

    semantics_ar_process_monitor_t *ProcessMonitors;
    FAST_MUTEX                      ProcessMonitorLock;

    semantics_ar_create_ring_entry_t *CreateRingEntries;
    volatile LONG                     CreateRingCursor;

    volatile LONG ConfirmedActive;
    BOOLEAN       ProcessNotifyRegistered;

    volatile LONGLONG ShadowSequence;

    volatile LONG CaptureWorkerCount;

    BCRYPT_ALG_HANDLE MacAlgHandle;
    ULONG             MacObjectLength;
    BOOLEAN           MacReady;
    UCHAR             MacKey[SEMANTICS_AR_MAC_KEY_SIZE];
} semantics_ar_global_data_t;

extern semantics_ar_global_data_t semantics_ar_globals;

DRIVER_INITIALIZE DriverEntry;

NTSTATUS semantics_ar_filter_unload(_In_ FLT_FILTER_UNLOAD_FLAGS Flags);

NTSTATUS semantics_ar_instance_setup(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ DEVICE_TYPE VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType);

VOID semantics_ar_instance_teardown_start(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Reason);

VOID semantics_ar_instance_teardown_complete(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Reason);

FLT_PREOP_CALLBACK_STATUS semantics_ar_pre_create(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext);

FLT_POSTOP_CALLBACK_STATUS semantics_ar_post_create(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags);

FLT_PREOP_CALLBACK_STATUS semantics_ar_pre_write(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext);

FLT_PREOP_CALLBACK_STATUS semantics_ar_pre_set_information(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext);

FLT_PREOP_CALLBACK_STATUS semantics_ar_pre_fsctl(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext);

FLT_PREOP_CALLBACK_STATUS semantics_ar_pre_acquire_section(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext);

NTSTATUS semantics_ar_get_or_create_stream_context(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Out_ semantics_ar_stream_context_t **Context);

VOID semantics_ar_stream_context_cleanup(
    _In_ PFLT_CONTEXT Context,
    _In_ FLT_CONTEXT_TYPE ContextType);

BOOLEAN semantics_ar_is_trusted_pid(_In_ ULONG Pid);
VOID    semantics_ar_add_trusted_pid(_In_ ULONG Pid, _In_ LONGLONG CreationTime);
VOID    semantics_ar_remove_trusted_pid(_In_ ULONG Pid, _In_ LONGLONG CreationTime);

semantics_ar_process_monitor_t *semantics_ar_monitor_find(_In_ ULONG Pid);
semantics_ar_process_monitor_t *semantics_ar_monitor_allocate(_In_ ULONG Pid);
VOID semantics_ar_monitor_release(_Inout_ semantics_ar_process_monitor_t *Monitor);
BOOLEAN semantics_ar_is_confirmed_process(_In_ ULONG Pid);
VOID semantics_ar_confirm_process_tree(_In_ ULONG TargetPid);
VOID semantics_ar_release_confirmed_tree_locked(_In_ ULONG TargetPid);
VOID semantics_ar_associate_child_locked(_In_ ULONG ChildPid, _In_ ULONG OriginatorPid);
BOOLEAN semantics_ar_file_self_created(_In_ ULONG Pid, _In_ UINT64 FileId);

VOID semantics_ar_process_notify(
    _Inout_ PEPROCESS Process,
    _In_ HANDLE ProcessId,
    _Inout_opt_ PPS_CREATE_NOTIFY_INFO CreateInfo);

NTSTATUS semantics_ar_create_comm_port(_In_ PFLT_FILTER Filter);
VOID semantics_ar_close_comm_port(VOID);

VOID semantics_ar_drain_journal_all_instances(VOID);

NTSTATUS semantics_ar_mac_init(VOID);
VOID semantics_ar_mac_cleanup(VOID);
NTSTATUS semantics_ar_mac_compute(
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _Out_writes_bytes_(SEMANTICS_AR_INTEGRITY_TAG_SIZE) UCHAR *Tag);

#endif