#ifndef SEMANTICS_AR_DRIVER_SEAM_H
#define SEMANTICS_AR_DRIVER_SEAM_H

#include <fltKernel.h>

#include "driver.h"

#define SAR_STREAMCTX_FLAG_SECTION_DIRTY 0x00000001u
#define SAR_STREAMCTX_FLAG_OWN_STORE     0x00000002u
#define SAR_STREAMCTX_FLAG_PHANTOM_BACKING 0x00000004u
#define SAR_STREAMCTX_FLAG_BASELINE_STAGED 0x00000008u

typedef struct _SAR_STREAM_CONTEXT {
    volatile LONG flags;
    volatile LONG read_length;
    UINT64        read_offset;
    UCHAR         read_sample[SAR_CAPTURE_BUFFER_BYTES];
} SAR_STREAM_CONTEXT, *PSAR_STREAM_CONTEXT;

typedef struct _SAR_CAPTURE_BUFFER {
    UCHAR pre_image[SAR_CAPTURE_BUFFER_BYTES];
    ULONG pre_image_length;
    UCHAR written[SAR_CAPTURE_BUFFER_BYTES];
    ULONG written_length;
} SAR_CAPTURE_BUFFER, *PSAR_CAPTURE_BUFFER;

typedef struct _SAR_CONTINUATION {
    KEVENT register_grab_done;
    PIO_WORKITEM deferred_work;
    BOOLEAN deferred_queued;
} SAR_CONTINUATION, *PSAR_CONTINUATION;

typedef struct _SAR_WRITE_SEAM_REQUEST {
    PFLT_CALLBACK_DATA data;
    PCFLT_RELATED_OBJECTS related;
    PEPROCESS originator_process;
    PETHREAD originator_thread;
    PSAR_STREAM_CONTEXT stream_context;
    sar_destruct_member_t member;
    ULONG irp_flags;
    LARGE_INTEGER write_offset;
    ULONG write_length;
    ULONG pre_image_len;
    UCHAR pre_image[SAR_CAPTURE_BUFFER_BYTES];
    SAR_CONTINUATION continuation;
    PSAR_CAPTURE_BUFFER capture_buffer;
} SAR_WRITE_SEAM_REQUEST, *PSAR_WRITE_SEAM_REQUEST;

typedef struct _SAR_METADATA_SEAM_REQUEST {
    PFLT_CALLBACK_DATA data;
    PCFLT_RELATED_OBJECTS related;
    sar_destruct_member_t member;
    ULONG fs_control_code;
} SAR_METADATA_SEAM_REQUEST, *PSAR_METADATA_SEAM_REQUEST;

typedef struct _SAR_SEAM_COVERAGE {
    volatile LONG64 capture_seam_counts[SAR_DESTRUCT_MEMBER_COUNT];
    volatile LONG64 metadata_seam_counts[SAR_DESTRUCT_MEMBER_COUNT];
} SAR_SEAM_COVERAGE, *PSAR_SEAM_COVERAGE;

_IRQL_requires_max_(APC_LEVEL)
VOID SarSeamCoverageReset(VOID);

_IRQL_requires_max_(APC_LEVEL)
VOID SarSeamWriteSubmit(_Inout_ PSAR_WRITE_SEAM_REQUEST Request);

_IRQL_requires_max_(APC_LEVEL)
VOID SarSeamMetadataSubmit(_Inout_ PSAR_METADATA_SEAM_REQUEST Request);

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID SarSeamWriteRelease(_Inout_ PSAR_WRITE_SEAM_REQUEST Request);

#endif
