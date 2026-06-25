#ifndef SEMANTICS_AR_DRIVER_NTSYSTEM_H
#define SEMANTICS_AR_DRIVER_NTSYSTEM_H

#include <ntddk.h>

#define SystemCodeIntegrityInformation      0x67
#define SystemIsolatedUserModeInformation   0xA5

#ifndef CODEINTEGRITY_OPTION_HVCI_KMCI_ENABLED
#define CODEINTEGRITY_OPTION_HVCI_KMCI_ENABLED 0x00000400
#endif

#ifndef PROCESS_QUERY_LIMITED_INFORMATION
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#endif

typedef struct _SYSTEM_CODEINTEGRITY_INFORMATION {
    ULONG Length;
    ULONG CodeIntegrityOptions;
} SYSTEM_CODEINTEGRITY_INFORMATION, *PSYSTEM_CODEINTEGRITY_INFORMATION;

typedef struct _SYSTEM_ISOLATED_USER_MODE_INFORMATION {
    BOOLEAN SecureKernelRunning : 1;
    BOOLEAN HvciEnabled : 1;
    BOOLEAN HvciStrictMode : 1;
    BOOLEAN DebugEnabled : 1;
    BOOLEAN FirmwarePageProtection : 1;
    BOOLEAN EncryptionKeyAvailable : 1;
    BOOLEAN SpareFlags : 2;
    BOOLEAN TrustletRunning : 1;
    BOOLEAN HvciDisableAllowed : 1;
    BOOLEAN HardwareEnforcedVbs : 1;
    BOOLEAN NoSecrets : 1;
    BOOLEAN EncryptionKeyPersistent : 1;
    BOOLEAN HardwareEnforcedHvpt : 1;
    BOOLEAN HardwareHvptAvailable : 1;
    BOOLEAN SpareFlags2 : 1;
    BOOLEAN Spare0[6];
    ULONGLONG Spare1;
} SYSTEM_ISOLATED_USER_MODE_INFORMATION, *PSYSTEM_ISOLATED_USER_MODE_INFORMATION;

typedef enum _PS_PROTECTED_TYPE {
    PsProtectedTypeNone = 0,
    PsProtectedTypeProtectedLight = 1,
    PsProtectedTypeProtected = 2
} PS_PROTECTED_TYPE;

typedef enum _PS_PROTECTED_SIGNER {
    PsProtectedSignerNone = 0,
    PsProtectedSignerAuthenticode = 1,
    PsProtectedSignerCodeGen = 2,
    PsProtectedSignerAntimalware = 3,
    PsProtectedSignerLsa = 4,
    PsProtectedSignerWindows = 5,
    PsProtectedSignerWinTcb = 6,
    PsProtectedSignerWinSystem = 7,
    PsProtectedSignerApp = 8
} PS_PROTECTED_SIGNER;

typedef struct _PS_PROTECTION {
    union {
        UCHAR Level;
        struct {
            UCHAR Type : 3;
            UCHAR Audit : 1;
            UCHAR Signer : 4;
        };
    };
} PS_PROTECTION, *PPS_PROTECTION;

NTSTATUS NTAPI ZwQuerySystemInformation(ULONG SystemInformationClass,
                                        PVOID SystemInformation,
                                        ULONG SystemInformationLength,
                                        PULONG ReturnLength);

NTSTATUS NTAPI ZwQueryInformationProcess(HANDLE ProcessHandle,
                                         PROCESSINFOCLASS ProcessInformationClass,
                                         PVOID ProcessInformation,
                                         ULONG ProcessInformationLength,
                                         PULONG ReturnLength);

NTKERNELAPI NTSTATUS NTAPI PsAcquireProcessExitSynchronization(PEPROCESS Process);

NTKERNELAPI VOID NTAPI PsReleaseProcessExitSynchronization(PEPROCESS Process);

typedef struct _SAR_PROCESS_BASIC_INFORMATION {
    NTSTATUS ExitStatus;
    PVOID PebBaseAddress;
    ULONG_PTR AffinityMask;
    LONG BasePriority;
    ULONG_PTR UniqueProcessId;
    ULONG_PTR InheritedFromUniqueProcessId;
} SAR_PROCESS_BASIC_INFORMATION;

#endif
