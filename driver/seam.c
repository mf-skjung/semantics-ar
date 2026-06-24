#include "seam.h"

static SAR_SEAM_COVERAGE g_sar_seam_coverage;

_IRQL_requires_max_(APC_LEVEL)
VOID SarSeamCoverageReset(VOID)
{
    RtlZeroMemory(&g_sar_seam_coverage, sizeof(g_sar_seam_coverage));
}

_IRQL_requires_max_(APC_LEVEL)
VOID SarSeamWriteSubmit(_Inout_ PSAR_WRITE_SEAM_REQUEST Request)
{
    NT_ASSERT(Request != NULL);
    NT_ASSERT(Request->member > SAR_DESTRUCT_NONE);
    NT_ASSERT(Request->member < SAR_DESTRUCT_MEMBER_COUNT);
    NT_ASSERT(Request->originator_process != NULL);
    NT_ASSERT(Request->originator_thread != NULL);

    InterlockedIncrement64(&g_sar_seam_coverage.capture_seam_counts[Request->member]);
}

_IRQL_requires_max_(APC_LEVEL)
VOID SarSeamMetadataSubmit(_Inout_ PSAR_METADATA_SEAM_REQUEST Request)
{
    NT_ASSERT(Request != NULL);
    NT_ASSERT(Request->member > SAR_DESTRUCT_NONE);
    NT_ASSERT(Request->member < SAR_DESTRUCT_MEMBER_COUNT);

    InterlockedIncrement64(&g_sar_seam_coverage.metadata_seam_counts[Request->member]);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID SarSeamWriteRelease(_Inout_ PSAR_WRITE_SEAM_REQUEST Request)
{
    if (Request == NULL)
        return;

    if (Request->originator_thread != NULL) {
        ObDereferenceObject(Request->originator_thread);
        Request->originator_thread = NULL;
    }
    if (Request->originator_process != NULL) {
        ObDereferenceObject(Request->originator_process);
        Request->originator_process = NULL;
    }
    if (Request->capture_buffer != NULL) {
        ExFreePoolWithTag(Request->capture_buffer, SAR_POOL_TAG_CAPBUF);
        Request->capture_buffer = NULL;
    }
    if (Request->continuation.deferred_work != NULL) {
        IoFreeWorkItem(Request->continuation.deferred_work);
        Request->continuation.deferred_work = NULL;
    }
}
