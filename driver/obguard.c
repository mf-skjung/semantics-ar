#include "driver.h"
#include "state.h"
#include "eventlog.h"
#include "obguard.h"

#include <ntifs.h>

extern PSAR_STATE g_sar_state;

#ifndef PROCESS_CREATE_THREAD
#define PROCESS_CREATE_THREAD 0x0002
#endif
#ifndef PROCESS_VM_OPERATION
#define PROCESS_VM_OPERATION 0x0008
#endif
#ifndef PROCESS_VM_WRITE
#define PROCESS_VM_WRITE 0x0020
#endif
#ifndef PROCESS_DUP_HANDLE
#define PROCESS_DUP_HANDLE 0x0040
#endif
#ifndef PROCESS_CREATE_PROCESS
#define PROCESS_CREATE_PROCESS 0x0080
#endif
#ifndef PROCESS_SET_INFORMATION
#define PROCESS_SET_INFORMATION 0x0200
#endif
#ifndef PROCESS_SUSPEND_RESUME
#define PROCESS_SUSPEND_RESUME 0x0800
#endif
#ifndef THREAD_SUSPEND_RESUME
#define THREAD_SUSPEND_RESUME 0x0002
#endif
#ifndef THREAD_SET_CONTEXT
#define THREAD_SET_CONTEXT 0x0010
#endif
#ifndef THREAD_SET_INFORMATION
#define THREAD_SET_INFORMATION 0x0020
#endif
#ifndef THREAD_SET_THREAD_TOKEN
#define THREAD_SET_THREAD_TOKEN 0x0080
#endif
#ifndef THREAD_IMPERSONATE
#define THREAD_IMPERSONATE 0x0100
#endif
#ifndef THREAD_DIRECT_IMPERSONATION
#define THREAD_DIRECT_IMPERSONATION 0x0200
#endif

#define SAR_PROCESS_INJECT_MASK \
    (PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_CREATE_THREAD | \
     PROCESS_DUP_HANDLE | PROCESS_CREATE_PROCESS | PROCESS_SET_INFORMATION | \
     PROCESS_SUSPEND_RESUME)

#define SAR_THREAD_INJECT_MASK \
    (THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_SET_INFORMATION | \
     THREAD_SET_THREAD_TOKEN | THREAD_IMPERSONATE | THREAD_DIRECT_IMPERSONATION)

static PVOID g_sar_ob_handle;
static OB_OPERATION_REGISTRATION g_sar_ob_ops[2];

_IRQL_requires_max_(PASSIVE_LEVEL)
static OB_PREOP_CALLBACK_STATUS SarObPreOperation(_In_ PVOID RegistrationContext,
                                                  _In_ POB_PRE_OPERATION_INFORMATION Info)
{
    PEPROCESS target;
    PEPROCESS opener;
    PACCESS_MASK desired;
    ACCESS_MASK strip;
    UINT64 start_key = 0;

    UNREFERENCED_PARAMETER(RegistrationContext);

    if (g_sar_state == NULL)
        return OB_PREOP_SUCCESS;
    if (Info->KernelHandle)
        return OB_PREOP_SUCCESS;

    if (Info->ObjectType == *PsProcessType) {
        target = (PEPROCESS)Info->Object;
        strip = SAR_PROCESS_INJECT_MASK;
    } else if (Info->ObjectType == *PsThreadType) {
        target = PsGetThreadProcess((PETHREAD)Info->Object);
        strip = SAR_THREAD_INJECT_MASK;
    } else {
        return OB_PREOP_SUCCESS;
    }

    if (target == NULL)
        return OB_PREOP_SUCCESS;

    if (!SarStateProtectedTarget(g_sar_state, target, &start_key))
        return OB_PREOP_SUCCESS;

    if (Info->Operation == OB_OPERATION_HANDLE_CREATE) {
        opener = PsGetCurrentProcess();
        desired = &Info->Parameters->CreateHandleInformation.DesiredAccess;
    } else if (Info->Operation == OB_OPERATION_HANDLE_DUPLICATE) {
        opener = (PEPROCESS)Info->Parameters->DuplicateHandleInformation.TargetProcess;
        desired = &Info->Parameters->DuplicateHandleInformation.DesiredAccess;
    } else {
        return OB_PREOP_SUCCESS;
    }

    if (opener == target)
        return OB_PREOP_SUCCESS;
    if (opener != NULL && SarStateOpenerTrusted(g_sar_state, opener))
        return OB_PREOP_SUCCESS;

    if ((*desired & strip) == 0)
        return OB_PREOP_SUCCESS;

    if (SarStateModeGet(g_sar_state) == SEMANTICS_AR_MODE_ENFORCE)
        *desired &= ~strip;

    SarEventLogRecord(g_sar.eventlog, SAR_EVENT_CLASS_BLOCK_INJECTION, start_key);
    return OB_PREOP_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarObGuardRegister(VOID)
{
    OB_CALLBACK_REGISTRATION reg;
    UNICODE_STRING altitude;

    g_sar_ob_ops[0].ObjectType = PsProcessType;
    g_sar_ob_ops[0].Operations = OB_OPERATION_HANDLE_CREATE | OB_OPERATION_HANDLE_DUPLICATE;
    g_sar_ob_ops[0].PreOperation = SarObPreOperation;
    g_sar_ob_ops[0].PostOperation = NULL;
    g_sar_ob_ops[1].ObjectType = PsThreadType;
    g_sar_ob_ops[1].Operations = OB_OPERATION_HANDLE_CREATE | OB_OPERATION_HANDLE_DUPLICATE;
    g_sar_ob_ops[1].PreOperation = SarObPreOperation;
    g_sar_ob_ops[1].PostOperation = NULL;

    RtlInitUnicodeString(&altitude, L"385000");

    reg.Version = OB_FLT_REGISTRATION_VERSION;
    reg.OperationRegistrationCount = 2;
    reg.Altitude = altitude;
    reg.RegistrationContext = NULL;
    reg.OperationRegistration = g_sar_ob_ops;

    return ObRegisterCallbacks(&reg, &g_sar_ob_handle);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarObGuardUnregister(VOID)
{
    if (g_sar_ob_handle == NULL)
        return;

    ObUnRegisterCallbacks(g_sar_ob_handle);
    g_sar_ob_handle = NULL;
}
