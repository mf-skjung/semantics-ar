#ifndef SEMANTICS_AR_SERVICE_ATTRIB_H
#define SEMANTICS_AR_SERVICE_ATTRIB_H

#include <stdint.h>

#include "control.h"

void sar_attrib_init(void);

uint64_t sar_attrib_resolve(const uint16_t *nt_image_path);

uint32_t sar_attrib_count(void);

int sar_attrib_enumerate(uint32_t index, sar_app_identity_entry_t *out);

#endif
