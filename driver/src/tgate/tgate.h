#ifndef SEMANTICS_AR_TGATE_H
#define SEMANTICS_AR_TGATE_H

#include "driver_internal.h"

semantics_ar_tgate_verdict_t semantics_ar_tgate_classify(
    _In_reads_bytes_(OldLen) const UCHAR *OldData,
    _In_ ULONG OldLen,
    _In_reads_bytes_(NewLen) const UCHAR *NewData,
    _In_ ULONG NewLen);

#endif