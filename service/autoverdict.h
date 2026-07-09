#ifndef SEMANTICS_AR_SERVICE_AUTOVERDICT_H
#define SEMANTICS_AR_SERVICE_AUTOVERDICT_H

#include <windows.h>
#include <stdint.h>

#include "commclient.h"
#include "identity.h"

void sar_autoverdict_note(const wchar_t *path, int add);

sar_identity_verdict_t sar_verdict_pid(sar_comm_client_t *client, uint32_t pid,
                                       uint32_t *out_id_state, uint64_t *out_start_key);

int sar_autoverdict_start(sar_comm_client_t *client, CRITICAL_SECTION *comm_lock);

void sar_autoverdict_stop(void);

#endif
