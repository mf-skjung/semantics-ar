#include <windows.h>
#include <tbs.h>

#include "posture.h"
#include "semantics_ar/posture.h"

#define SAR_SI_ISOLATED_USER_MODE 0xA5u
#define SAR_IUM_SECURE_KERNEL     0x01u
#define SAR_IUM_HVCI_ENABLED      0x02u

typedef LONG(WINAPI *sar_nt_query_si_fn)(ULONG, PVOID, ULONG, PULONG);

typedef struct {
    UCHAR     flags0;
    UCHAR     flags1;
    UCHAR     spare0[6];
    ULONGLONG spare1;
} sar_ium_information_t;

static sar_nt_query_si_fn sar_resolve_nt_query_si(void)
{
    static sar_nt_query_si_fn fn = NULL;
    HMODULE ntdll;

    if (fn)
        return fn;

    ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll)
        fn = (sar_nt_query_si_fn)GetProcAddress(ntdll, "NtQuerySystemInformation");
    return fn;
}

static BOOL sar_hvci_running(void)
{
    sar_nt_query_si_fn   query = sar_resolve_nt_query_si();
    sar_ium_information_t ium;
    ULONG                ret = 0;

    if (!query)
        return FALSE;

    memset(&ium, 0, sizeof(ium));
    if (query(SAR_SI_ISOLATED_USER_MODE, &ium, (ULONG)sizeof(ium), &ret) != 0)
        return FALSE;
    if (ret < sizeof(ium))
        return FALSE;

    return (ium.flags0 & SAR_IUM_SECURE_KERNEL)
        && (ium.flags0 & SAR_IUM_HVCI_ENABLED);
}

static BOOL sar_tpm20_present(void)
{
    TPM_DEVICE_INFO info;

    memset(&info, 0, sizeof(info));
    if (Tbsi_GetDeviceInfo((UINT32)sizeof(info), &info) != TBS_SUCCESS)
        return FALSE;
    return info.tpmVersion == TPM_VERSION_20;
}

static BOOL sar_self_protected(void)
{
    PROCESS_PROTECTION_LEVEL_INFORMATION level;

    memset(&level, 0, sizeof(level));
    if (!GetProcessInformation(GetCurrentProcess(), ProcessProtectionLevelInfo,
                               &level, (DWORD)sizeof(level)))
        return FALSE;
    return level.ProtectionLevel != PROTECTION_LEVEL_NONE;
}

uint32_t sar_posture_descents(void)
{
    uint32_t descents = 0;

    if (!sar_tpm20_present())
        descents |= SAR_POSTURE_DESCENT_NO_TPM;
    if (!sar_hvci_running())
        descents |= SAR_POSTURE_DESCENT_NO_VBS_HVCI;
    if (!sar_self_protected())
        descents |= SAR_POSTURE_DESCENT_NO_PPL;

    return descents;
}
