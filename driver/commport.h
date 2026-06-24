#ifndef SEMANTICS_AR_DRIVER_COMMPORT_H
#define SEMANTICS_AR_DRIVER_COMMPORT_H

#include <fltKernel.h>

#include "driver.h"

#define SAR_COMM_PORT_NAME L"\\SemanticsArPort"

typedef struct _SAR_COMM {
    PFLT_FILTER filter;
    PFLT_PORT server_port;
    PFLT_PORT client_port;
    sar_handshake_t handshake;
    volatile LONG64 tamper_counter;
} SAR_COMM, *PSAR_COMM;

#endif
