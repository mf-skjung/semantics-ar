#include "driver_internal.h"

BOOLEAN semantics_ar_is_trusted_pid(ULONG Pid)
{
    for (LONG i = 0; i < SEMANTICS_AR_MAX_TRUSTED_PIDS; i++) {
        if (semantics_ar_globals.TrustedPids[i].Pid == Pid) {
            PEPROCESS process;
            NTSTATUS status = PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)Pid, &process);
            if (!NT_SUCCESS(status))
                return FALSE;
            LONGLONG currentTime = PsGetProcessCreateTimeQuadPart(process);
            ObDereferenceObject(process);
            return (currentTime == semantics_ar_globals.TrustedPids[i].CreationTime);
        }
    }
    return FALSE;
}

VOID semantics_ar_add_trusted_pid(ULONG Pid, LONGLONG CreationTime)
{
    for (LONG i = 0; i < SEMANTICS_AR_MAX_TRUSTED_PIDS; i++) {
        if (semantics_ar_globals.TrustedPids[i].Pid == Pid)
            return;
    }
    for (LONG i = 0; i < SEMANTICS_AR_MAX_TRUSTED_PIDS; i++) {
        if (semantics_ar_globals.TrustedPids[i].Pid == 0) {
            semantics_ar_globals.TrustedPids[i].CreationTime = CreationTime;
            MemoryBarrier();
            if (InterlockedCompareExchange(
                    (PLONG)&semantics_ar_globals.TrustedPids[i].Pid, (LONG)Pid, 0) == 0)
                return;
        }
    }
}

VOID semantics_ar_remove_trusted_pid(ULONG Pid, LONGLONG CreationTime)
{
    for (LONG i = 0; i < SEMANTICS_AR_MAX_TRUSTED_PIDS; i++) {
        if (semantics_ar_globals.TrustedPids[i].Pid == Pid &&
            semantics_ar_globals.TrustedPids[i].CreationTime == CreationTime) {
            semantics_ar_globals.TrustedPids[i].CreationTime = 0;
            InterlockedExchange((PLONG)&semantics_ar_globals.TrustedPids[i].Pid, 0);
            return;
        }
    }
}