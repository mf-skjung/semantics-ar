#ifndef SARAPI_SERVER_IDENTITY_H
#define SARAPI_SERVER_IDENTITY_H

#include <windows.h>

#include "sarapi.h"

sarapi_result_t sarapi_server_is_system(HANDLE pipe);

sarapi_result_t sarapi_read_frame(HANDLE pipe, void *out, DWORD len, DWORD *got);

#endif
