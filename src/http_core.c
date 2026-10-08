#define _DEFAULT_SOURCE

#include "http_core.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>

#ifndef MSG_NOSIGNAL
#ifdef SO_NOSIGPIPE
#define OMNIPAD_USE_SO_NOSIGPIPE
#endif
#define MSG_NOSIGNAL 0
#endif

static const char *find_crlf(const char *start, const char *end)
{
    for (const char *p = start; p + 1 < end; p++) {
        if (p[0] == '\r' && p[1] == '\n') return p;
    }
    return NULL;
}

size_t http_header_length(const char *data, size_t length)
{
    if (!data) return 0;
    for (size_t i = 0; i + 3u < length; i++) {
        if (data[i] == '\r' && data[i + 1u] == '\n' &&
            data[i + 2u] == '\r' && data[i + 3u] == '\n') {
            return i + 4u;
        }
    }
    return 0;
}

static int copy_span(char *out, size_t capacity, const char *start,
                     size_t length)
{
    if (!out || capacity == 0 || length >= capacity) return 0;
    memcpy(out, start, length);
    out[length] = '\0';
    return 1;
}

static int parse_content_length(const char *value, size_t length, size_t *out)
{
    size_t parsed = 0;
    if (length == 0) return 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)value[i];
        if (c < '0' || c > '9') return 0;
        unsigned digit = (unsigned)(c - '0');
        if (parsed > (SIZE_MAX - digit) / 10u) return 0;
        parsed = parsed * 10u + digit;
    }
    *out = parsed;
    return 1;
}

static int header_name_char(unsigned char c)
{
    return isalnum(c) || c == '!' || c == '#' || c == '$' || c == '%' ||
           c == '&' || c == '\'' || c == '*' || c == '+' || c == '-' ||
           c == '.' || c == '^' || c == '_' || c == '`' || c == '|' || c == '~';
}

int http_request_parse(const char *data, size_t length, http_request_t *request)
{
    const char *end;
    const char *line_end;
    const char *sp1;
    const char *sp2;
    const char *cursor;
    size_t header_len;

    if (!data || !request) return 0;
    memset(request, 0, sizeof(*request));
    header_len = http_header_length(data, length);
    if (header_len == 0 || header_len != length) return 0;
    end = data + length;
    line_end = find_crlf(data, end);
    if (!line_end || line_end == data) return 0;

    sp1 = memchr(data, ' ', (size_t)(line_end - data));
    if (!sp1 || sp1 == data) return 0;
    sp2 = memchr(sp1 + 1, ' ', (size_t)(line_end - (sp1 + 1)));
    if (!sp2 || sp2 == sp1 + 1 || memchr(sp2 + 1, ' ',
                                         (size_t)(line_end - (sp2 + 1)))) {
        return 0;
    }
    if (!copy_span(request->method, sizeof(request->method), data,
                   (size_t)(sp1 - data)) ||
        !copy_span(request->target, sizeof(request->target), sp1 + 1,
                   (size_t)(sp2 - (sp1 + 1)))) {
        return 0;
    }
    for (const char *p = data; p < sp1; p++) {
        if (!header_name_char((unsigned char)*p)) return 0;
    }
    for (const char *p = sp1 + 1; p < sp2; p++) {
        unsigned char c = (unsigned char)*p;
        if (c <= 0x20u || c == 0x7fu || c == '#') return 0;
    }
    if (request->target[0] != '/') return 0;
    if ((size_t)(line_end - (sp2 + 1)) != 8u ||
        (memcmp(sp2 + 1, "HTTP/1.1", 8u) != 0 &&
         memcmp(sp2 + 1, "HTTP/1.0", 8u) != 0)) {
        return 0;
    }

    const char *query = strchr(request->target, '?');
    size_t path_len = query ? (size_t)(query - request->target) : strlen(request->target);
    if (!copy_span(request->path, sizeof(request->path), request->target, path_len)) {
        return 0;
    }

    cursor = line_end + 2;
    while (cursor < end) {
        const char *header_end = find_crlf(cursor, end);
        const char *colon;
        const char *value_start;
        const char *value_end;
        if (!header_end) return 0;
        if (header_end == cursor) return header_end + 2 == end;
        colon = memchr(cursor, ':', (size_t)(header_end - cursor));
        if (!colon || colon == cursor) return 0;
        for (const char *p = cursor; p < colon; p++) {
            if (!header_name_char((unsigned char)*p)) return 0;
        }
        value_start = colon + 1;
        while (value_start < header_end && (*value_start == ' ' || *value_start == '\t')) value_start++;
        value_end = header_end;
        while (value_end > value_start && (value_end[-1] == ' ' || value_end[-1] == '\t')) value_end--;
        for (const char *p = value_start; p < value_end; p++) {
            unsigned char c = (unsigned char)*p;
            if ((c < 0x20u && c != '\t') || c == 0x7fu) return 0;
        }

        size_t name_len = (size_t)(colon - cursor);
        size_t value_len = (size_t)(value_end - value_start);
        if (name_len == 4u && strncasecmp(cursor, "Host", 4u) == 0) {
            if (request->has_host || value_len == 0u ||
                !copy_span(request->host, sizeof(request->host),
                           value_start, value_len)) return 0;
            request->has_host = 1;
        } else if (name_len == 6u && strncasecmp(cursor, "Origin", 6u) == 0) {
            if (request->has_origin || value_len == 0u ||
                !copy_span(request->origin, sizeof(request->origin),
                           value_start, value_len)) return 0;
            request->has_origin = 1;
        } else if (name_len == 13u && strncasecmp(cursor, "Authorization", 13u) == 0) {
            if (request->has_authorization || value_len < 7u ||
                strncasecmp(value_start, "Bearer ", 7u) != 0 ||
                value_len - 7u == 0u || value_len - 7u > HTTP_AUTH_MAX) {
                return 0;
            }
            if (!copy_span(request->authorization, sizeof(request->authorization),
                           value_start + 7u, value_len - 7u)) return 0;
            request->has_authorization = 1;
        } else if (name_len == 14u && strncasecmp(cursor, "Content-Length", 14u) == 0) {
            if (request->has_content_length ||
                !parse_content_length(value_start, value_len, &request->content_length)) {
                return 0;
            }
            request->has_content_length = 1;
        } else if (name_len == 17u && strncasecmp(cursor, "Transfer-Encoding", 17u) == 0) {
            return 0;
        }
        cursor = header_end + 2;
    }
    return 0;
}

static int hex_value(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int query_decode(const char *start, size_t length, char *out, size_t capacity)
{
    size_t used = 0;
    if (!out || capacity == 0) return 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)start[i];
        if (c == '+') {
            c = ' ';
        } else if (c == '%') {
            if (i + 2u >= length) return 0;
            int hi = hex_value((unsigned char)start[i + 1u]);
            int lo = hex_value((unsigned char)start[i + 2u]);
            if (hi < 0 || lo < 0) return 0;
            c = (unsigned char)((hi << 4) | lo);
            i += 2u;
        }
        if (c == '\0' || c < 0x20u || c == 0x7fu || used + 1u >= capacity) return 0;
        out[used++] = (char)c;
    }
    out[used] = '\0';
    return 1;
}

int http_query_get(const http_request_t *request, const char *key,
                   char *value, size_t value_capacity)
{
    const char *query;
    const char *cursor;
    size_t key_len;
    int found = 0;

    if (!request || !key || !value || value_capacity == 0) return -1;
    query = strchr(request->target, '?');
    if (!query) return 0;
    cursor = query + 1;
    key_len = strlen(key);
    while (*cursor) {
        const char *end = strchr(cursor, '&');
        const char *equals;
        size_t component_len;
        if (!end) end = cursor + strlen(cursor);
        component_len = (size_t)(end - cursor);
        equals = memchr(cursor, '=', component_len);
        if (equals && (size_t)(equals - cursor) == key_len &&
            memcmp(cursor, key, key_len) == 0) {
            if (found || !query_decode(equals + 1,
                    (size_t)(end - (equals + 1)), value, value_capacity)) {
                return -1;
            }
            found = 1;
        }
        if (*end == '\0') break;
        cursor = end + 1;
    }
    return found;
}

int http_parse_long(const char *value, int base, long minimum, long maximum,
                    long *result)
{
    char *end;
    long parsed;
    if (!value || !*value || !result || base < 0 || base == 1 || base > 36) return 0;
    errno = 0;
    parsed = strtol(value, &end, base);
    if (errno == ERANGE || end == value || *end != '\0' ||
        parsed < minimum || parsed > maximum) return 0;
    *result = parsed;
    return 1;
}

void http_builder_init(http_builder_t *builder, char *buffer, size_t capacity)
{
    if (!builder) return;
    builder->data = buffer;
    builder->capacity = capacity;
    builder->length = 0;
    builder->failed = (!buffer || capacity == 0);
    if (buffer && capacity > 0) buffer[0] = '\0';
}

int http_builder_append(http_builder_t *builder, const char *text)
{
    size_t length;
    if (!builder || !text || builder->failed) return 0;
    length = strlen(text);
    if (builder->length >= builder->capacity ||
        length >= builder->capacity - builder->length) {
        builder->failed = 1;
        if (builder->capacity > 0) builder->data[builder->capacity - 1u] = '\0';
        return 0;
    }
    memcpy(builder->data + builder->length, text, length + 1u);
    builder->length += length;
    return 1;
}

int http_builder_appendf(http_builder_t *builder, const char *format, ...)
{
    va_list ap;
    int required;
    size_t remaining;
    if (!builder || !format || builder->failed ||
        builder->length >= builder->capacity) return 0;
    remaining = builder->capacity - builder->length;
    va_start(ap, format);
    required = vsnprintf(builder->data + builder->length, remaining, format, ap);
    va_end(ap);
    if (required < 0 || (size_t)required >= remaining) {
        builder->failed = 1;
        builder->data[builder->capacity - 1u] = '\0';
        return 0;
    }
    builder->length += (size_t)required;
    return 1;
}

static int append_hex_escape(http_builder_t *builder, unsigned char c)
{
    static const char hex[] = "0123456789abcdef";
    char escaped[7] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 0x0f], '\0'};
    return http_builder_append(builder, escaped);
}

static size_t valid_utf8_sequence(const unsigned char *p, size_t remaining)
{
    unsigned char a, b, c, d;
    if (remaining == 0) return 0;
    a = p[0];
    if (a < 0x80u) return 1;
    if (a >= 0xc2u && a <= 0xdfu && remaining >= 2u &&
        (p[1] & 0xc0u) == 0x80u) return 2;
    if (a >= 0xe0u && a <= 0xefu && remaining >= 3u) {
        b = p[1]; c = p[2];
        if ((b & 0xc0u) != 0x80u || (c & 0xc0u) != 0x80u) return 0;
        if ((a == 0xe0u && b < 0xa0u) || (a == 0xedu && b >= 0xa0u)) return 0;
        return 3;
    }
    if (a >= 0xf0u && a <= 0xf4u && remaining >= 4u) {
        b = p[1]; c = p[2]; d = p[3];
        if ((b & 0xc0u) != 0x80u || (c & 0xc0u) != 0x80u ||
            (d & 0xc0u) != 0x80u) return 0;
        if ((a == 0xf0u && b < 0x90u) || (a == 0xf4u && b > 0x8fu)) return 0;
        return 4;
    }
    return 0;
}

int http_builder_append_json_string(http_builder_t *builder, const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    size_t length;
    if (!builder || !text || !http_builder_append(builder, "\"")) return 0;
    length = strlen(text);
    for (size_t i = 0; i < length;) {
        unsigned char c = p[i];
        if (c == '"' || c == '\\') {
            char escaped[3] = {'\\', (char)c, '\0'};
            if (!http_builder_append(builder, escaped)) return 0;
            i++;
        } else if (c < 0x20u) {
            const char *short_escape = NULL;
            switch (c) {
            case '\b': short_escape = "\\b"; break;
            case '\f': short_escape = "\\f"; break;
            case '\n': short_escape = "\\n"; break;
            case '\r': short_escape = "\\r"; break;
            case '\t': short_escape = "\\t"; break;
            default: break;
            }
            if (short_escape) {
                if (!http_builder_append(builder, short_escape)) return 0;
            } else if (!append_hex_escape(builder, c)) {
                return 0;
            }
            i++;
        } else if (c < 0x80u) {
            char plain[2] = {(char)c, '\0'};
            if (!http_builder_append(builder, plain)) return 0;
            i++;
        } else {
            size_t sequence_len = valid_utf8_sequence(p + i, length - i);
            if (sequence_len == 0) {
                if (!append_hex_escape(builder, c)) return 0;
                i++;
            } else {
                size_t remaining = builder->capacity - builder->length;
                if (sequence_len >= remaining) {
                    builder->failed = 1;
                    builder->data[builder->capacity - 1u] = '\0';
                    return 0;
                }
                memcpy(builder->data + builder->length, p + i, sequence_len);
                builder->length += sequence_len;
                builder->data[builder->length] = '\0';
                i += sequence_len;
            }
        }
    }
    return http_builder_append(builder, "\"");
}

int http_constant_time_equal(const char *left, const char *right)
{
    size_t left_len;
    size_t right_len;
    size_t max_len;
    unsigned diff;
    if (!left || !right) return 0;
    left_len = strlen(left);
    right_len = strlen(right);
    max_len = left_len > right_len ? left_len : right_len;
    diff = (unsigned)(left_len ^ right_len);
    for (size_t i = 0; i < max_len; i++) {
        unsigned char a = i < left_len ? (unsigned char)left[i] : 0;
        unsigned char b = i < right_len ? (unsigned char)right[i] : 0;
        diff |= (unsigned)(a ^ b);
    }
    return diff == 0;
}

int http_send_all(int fd, const void *data, size_t length)
{
    const char *cursor = (const char *)data;
    if ((!data && length != 0) || fd < 0) return 0;
#ifdef OMNIPAD_USE_SO_NOSIGPIPE
    int no_sigpipe = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE,
                   &no_sigpipe, sizeof(no_sigpipe)) != 0) return 0;
#endif
    while (length > 0) {
        ssize_t sent = send(fd, cursor, length, MSG_NOSIGNAL);
        if (sent < 0 && errno == EINTR) continue;
        if (sent <= 0) return 0;
        cursor += (size_t)sent;
        length -= (size_t)sent;
    }
    return 1;
}
