#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include "util.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <ctype.h>


long now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000L + tv.tv_usec / 1000L;
}

uint64_t uptime_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

void sleep_ms(int ms)
{
    if (ms <= 0) return;
    usleep((useconds_t)ms * 1000);
}

uint32_t crc32_le(uint32_t crc, const uint8_t *buf, size_t len)
{
    crc = ~crc;
    while (len--) {
        crc ^= *buf++;
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
    }
    return ~crc;
}

uint64_t hex_to_u64(const char *s)
{
    if (!s) return 0;
    while (*s == ' ' || *s == ':' || *s == '=') s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    uint64_t val = 0;
    while (isxdigit((unsigned char)*s)) {
        char c = *s++;
        val = (val << 4) | (uint64_t)(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
    }
    return val;
}

int hex_dump(const void *data, size_t len, char *out, size_t out_len)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t written = 0;
    for (size_t i = 0; i < len && written + 3 < out_len; i++) {
        written += snprintf(out + written, out_len - written, "%02x ", p[i]);
    }
    if (written < out_len) out[written] = '\0';
    return (int)written;
}


