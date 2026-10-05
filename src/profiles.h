#ifndef PROFILES_H
#define PROFILES_H

#include "pad_types.h"
#include <stdint.h>
#include <stddef.h>

int profiles_parse_report(uint16_t vid, uint16_t pid, const uint8_t *data, size_t len, pad_state_t *st);

#endif /* PROFILES_H */
