#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

long now_ms(void);
uint64_t uptime_ms(void);
void sleep_ms(int ms);
uint32_t crc32_le(uint32_t crc, const uint8_t *buf, size_t len);
uint64_t hex_to_u64(const char *s);
int hex_dump(const void *data, size_t len, char *out, size_t out_len);

#endif /* UTIL_H */
