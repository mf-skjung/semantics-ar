#include "tgate.h"

#define TGATE_HIST_SCALE 16

static ULONG estimate_model_bits(const UCHAR *data, ULONG len)
{
    ULONG freq[256];
    RtlZeroMemory(freq, sizeof(freq));
    for (ULONG i = 0; i < len; i++)
        freq[data[i]]++;

    ULONG runPenalty = 0;
    UCHAR prev = data[0];
    ULONG run = 1;
    for (ULONG i = 1; i < len; i++) {
        if (data[i] == prev) {
            run++;
        } else {
            if (run >= 4)
                runPenalty += run;
            prev = data[i];
            run = 1;
        }
    }
    if (run >= 4)
        runPenalty += run;

    ULONGLONG entropyAccum = 0;
    for (ULONG s = 0; s < 256; s++) {
        ULONG f = freq[s];
        if (f == 0)
            continue;
        ULONG bits = 0;
        ULONG q = len / f;
        while (q > 0) { bits++; q >>= 1; }
        entropyAccum += (ULONGLONG)f * bits;
    }

    ULONG modelBits = (ULONG)(entropyAccum);
    if (runPenalty > modelBits)
        modelBits = 0;
    else
        modelBits -= runPenalty;

    return modelBits;
}

semantics_ar_tgate_verdict_t semantics_ar_tgate_classify(
    const UCHAR *OldData, ULONG OldLen,
    const UCHAR *NewData, ULONG NewLen)
{
    if (OldLen < SEMANTICS_AR_MIN_INSPECT_SIZE || NewLen < SEMANTICS_AR_MIN_INSPECT_SIZE)
        return SEMANTICS_AR_TGATE_SKIP;

    ULONG block = SEMANTICS_AR_MIN_INSPECT_SIZE;

    ULONG oldBits = estimate_model_bits(OldData, block);
    ULONG newBits = estimate_model_bits(NewData, block);

    ULONG maxBits = block * 8;

    if (newBits * 100 < maxBits * 96)
        return SEMANTICS_AR_TGATE_SKIP;

    if (oldBits >= newBits)
        return SEMANTICS_AR_TGATE_SKIP;

    ULONG delta = newBits - oldBits;
    if (delta * 100 < maxBits * 12)
        return SEMANTICS_AR_TGATE_SKIP;

    return SEMANTICS_AR_TGATE_PRESERVE;
}