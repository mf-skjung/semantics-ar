#include "driver.h"

typedef NTSTATUS (NTAPI *SAR_FLT_VETO_BYPASS_IO)(PFLT_CALLBACK_DATA, PCFLT_RELATED_OBJECTS,
                                                 NTSTATUS, PCUNICODE_STRING);

static SAR_FLT_VETO_BYPASS_IO g_sar_flt_veto_bypass_io;
static BOOLEAN g_sar_bypass_resolved;

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarBypassIoResolve(_Inout_ PSAR_POSTURE Posture)
{
    if (g_sar_bypass_resolved)
        return;

    g_sar_flt_veto_bypass_io = (SAR_FLT_VETO_BYPASS_IO)FltGetRoutineAddress("FltVetoBypassIo");

    g_sar_bypass_resolved = TRUE;
    Posture->bypass_io_negotiated = (g_sar_flt_veto_bypass_io != NULL) ? TRUE : FALSE;
}

_IRQL_requires_max_(APC_LEVEL)
FLT_PREOP_CALLBACK_STATUS SarPreManageBypassIo(_Inout_ PFLT_CALLBACK_DATA Data,
                                               _In_ PCFLT_RELATED_OBJECTS FltObjects,
                                               _Flt_CompletionContext_Outptr_ PVOID *CompletionContext)
{
    UNREFERENCED_PARAMETER(CompletionContext);

    if (g_sar_flt_veto_bypass_io != NULL) {
        (VOID)g_sar_flt_veto_bypass_io(Data, FltObjects, STATUS_SUCCESS, NULL);
    }

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}
