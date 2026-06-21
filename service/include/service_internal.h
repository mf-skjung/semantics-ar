#ifndef SEMANTICS_AR_SERVICE_INTERNAL_H
#define SEMANTICS_AR_SERVICE_INTERNAL_H

#include <windows.h>
#include <fltUser.h>
#include "semantics_ar/protocol.h"
#include "semantics_ar/errors.h"
#include "semantics_ar/shadow_format.h"
#include "key_capture.h"

#define SEMANTICS_AR_SVC_NAME        L"SemanticsAR"
#define SEMANTICS_AR_SVC_PORT        L"\\SemanticsArPort"
#define SEMANTICS_AR_SVC_CONFIG_DIR  "C:\\ProgramData\\SemanticsAR\\"
#define SEMANTICS_AR_SVC_CONFIG_PATH SEMANTICS_AR_SVC_CONFIG_DIR "config.ini"
#define SEMANTICS_AR_SVC_WHITELIST_PATH SEMANTICS_AR_SVC_CONFIG_DIR "whitelist.dat"

#define SEMANTICS_AR_SVC_SHADOW_DIR_NAME L"$SemanticsAR_Shadow"

#define SEMANTICS_AR_MAX_WHITELIST_ENTRIES 256
#define SEMANTICS_AR_MAX_ACTIVE_PROCESSES  256
#define SEMANTICS_AR_MAX_PENDING_RESTORES  32
#define SEMANTICS_AR_MAX_VOLUME_MAPPINGS   32
#define SEMANTICS_AR_MAX_CONFIRMED_KEYS    64

#define SEMANTICS_AR_WL_NOT_LISTED  0
#define SEMANTICS_AR_WL_VERIFIED    1

#define SEMANTICS_AR_TRUST_TIER_PUBLISHER 1
#define SEMANTICS_AR_TRUST_TIER_HASH      2
#define SEMANTICS_AR_TRUST_TIER_SESSION   3

#define SEMANTICS_AR_MODE_AUDIT   0
#define SEMANTICS_AR_MODE_ENFORCE 1

#define SEMANTICS_AR_WL_MAGIC 0x57484C33

#define SEMANTICS_AR_EVICTION_HIGH_PERCENT 90
#define SEMANTICS_AR_EVICTION_LOW_PERCENT  80
#define SEMANTICS_AR_EVICTION_POLL_MS      10000

#define SEMANTICS_AR_EVICT_PRIORITY_WHITELISTED 1
#define SEMANTICS_AR_EVICT_PRIORITY_TERMINATED  2
#define SEMANTICS_AR_EVICT_PRIORITY_ACTIVE      3

typedef struct {
    DWORD timeout_ms;
    DWORD operation_mode;
} semantics_ar_svc_config_t;

typedef struct {
    WCHAR ExePath[MAX_PATH];
    BYTE  FileHash[32];
    DWORD TrustTier;
    WCHAR CertSubject[256];
    WCHAR TrustedExeName[64];
} semantics_ar_whitelist_entry_t;

typedef struct {
    DWORD  Pid;
    BOOL   Valid;
    DWORD  TrustTier;
    HANDLE ProcessHandle;
    HANDLE WaitHandle;
    WCHAR  ExePath[MAX_PATH];
} semantics_ar_active_process_t;

typedef struct {
    CRITICAL_SECTION Lock;
    DWORD EntryCount;
    semantics_ar_whitelist_entry_t Entries[SEMANTICS_AR_MAX_WHITELIST_ENTRIES];
    semantics_ar_active_process_t  Active[SEMANTICS_AR_MAX_ACTIVE_PROCESSES];
} semantics_ar_whitelist_state_t;

typedef struct {
    BOOL  Active;
    DWORD RootPid;
    DWORD TreePids[SEMANTICS_AR_MAX_ACTIVE_PROCESSES];
    DWORD TreeCount;
} semantics_ar_pending_restore_t;

typedef struct {
    WCHAR NtDevicePath[256];
    WCHAR DosPrefix[MAX_PATH];
    BOOL  Valid;
} semantics_ar_volume_mapping_t;

typedef struct {
    SRWLOCK Lock;
    DWORD   Count;
    semantics_ar_volume_mapping_t Entries[SEMANTICS_AR_MAX_VOLUME_MAPPINGS];
} semantics_ar_volume_cache_t;

typedef struct {
    uint8_t  key[128];
    uint32_t key_length;
    uint32_t algorithm;
    uint32_t mode;
    uint32_t thread_id;
    uint32_t register_index;
} svc_confirmed_key_t;

typedef struct {
    SERVICE_STATUS_HANDLE StatusHandle;
    SERVICE_STATUS Status;
    HANDLE StopEvent;
    HANDLE Port;
    HANDLE EvictionThread;
    semantics_ar_svc_config_t  Config;
    semantics_ar_whitelist_state_t Whitelist;
    SRWLOCK PendingRestoreLock;
    semantics_ar_pending_restore_t PendingRestores[SEMANTICS_AR_MAX_PENDING_RESTORES];
    semantics_ar_volume_cache_t VolumeCache;
} semantics_ar_svc_state_t;

extern semantics_ar_svc_state_t g_svc;

DWORD WINAPI svc_eviction_thread(LPVOID param);

HRESULT svc_send_config(HANDLE port, const semantics_ar_config_t *config);

void svc_apply_defaults(semantics_ar_svc_config_t *config);
BOOL svc_load_config(const char *path, semantics_ar_svc_config_t *config);

void svc_whitelist_init(void);
void svc_whitelist_cleanup(void);
int  svc_whitelist_check_and_verify(DWORD pid);
void svc_whitelist_scan_running(void);
BOOL svc_hash_file(const WCHAR *path, BYTE hash[32]);

BOOL svc_terminate_process(DWORD pid);

BOOL convert_nt_to_dos_path(const WCHAR *ntPath, WCHAR *dosPath, DWORD maxChars);
int  svc_journal_restore(const WCHAR *volumeDosPrefix, const DWORD *targetPids, DWORD pidCount);
DWORD svc_collect_process_tree(DWORD rootPid, DWORD *outPids, DWORD maxPids);
void svc_restore_all_volumes(const DWORD *targetPids, DWORD pidCount);
void svc_cleanup_all_volumes(const DWORD *targetPids, DWORD pidCount);
uint64_t svc_cleanup_shadow_files(const WCHAR *volumeDosPrefix, const DWORD *targetPids, DWORD pidCount);

void svc_add_pending_restore(DWORD rootPid, const DWORD *treePids, DWORD treeCount);
int  svc_execute_restore(DWORD rootPid);

void svc_volume_cache_init(void);
void svc_volume_cache_build(void);
void svc_reconcile_shadow_usage(void);
void svc_evict_shadow_if_needed(const WCHAR *volumeDosPrefix);

int  svc_filter_listener_start(const wchar_t *port_name);
void svc_filter_listener_stop(void);
HANDLE svc_filter_listener_get_port(void);

void svc_keystore_init(void);
void svc_keystore_cleanup(void);
void svc_keystore_store(const oracle_result_t *result);

void svc_chain_handler_init(void);
void svc_chain_handler_cleanup(void);
uint32_t svc_handle_chain_candidate(const semantics_ar_chain_notification_t *notif,
                                    semantics_ar_chain_response_t *response);
void svc_post_oracle_confirmation(HANDLE port, DWORD pid);
uint32_t svc_get_confirmed_keys(svc_confirmed_key_t *out_keys, uint32_t max_keys);

semantics_ar_result_t svc_install_driver(const WCHAR *driverPath);
semantics_ar_result_t svc_uninstall_driver(void);

semantics_ar_result_t svc_query_driver_status(BOOL *installed, BOOL *running);
semantics_ar_result_t svc_query_protection_status(HANDLE port,
                                                  semantics_ar_status_reply_t *reply);

HRESULT svc_push_driver_config(HANDLE port, const semantics_ar_svc_config_t *config);

#endif