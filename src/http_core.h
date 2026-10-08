#ifndef HTTP_CORE_H
#define HTTP_CORE_H

#include <stddef.h>
#include <sys/types.h>

#define HTTP_METHOD_MAX 8u
#define HTTP_TARGET_MAX 256u
#define HTTP_AUTH_MAX 128u
#define HTTP_HOST_MAX 128u
#define HTTP_ORIGIN_MAX 160u

typedef struct {
    char method[HTTP_METHOD_MAX];
    char target[HTTP_TARGET_MAX];
    char path[HTTP_TARGET_MAX];
    char authorization[HTTP_AUTH_MAX + 1u];
    char host[HTTP_HOST_MAX + 1u];
    char origin[HTTP_ORIGIN_MAX + 1u];
    size_t content_length;
    int has_authorization;
    int has_content_length;
    int has_host;
    int has_origin;
} http_request_t;

typedef struct {
    char *data;
    size_t capacity;
    size_t length;
    int failed;
} http_builder_t;

/* Returns the header length including CRLFCRLF, or zero if incomplete. */
size_t http_header_length(const char *data, size_t length);

/* Parse a complete, body-free HTTP/1.0 or HTTP/1.1 request. */
int http_request_parse(const char *data, size_t length, http_request_t *request);

/* Query lookup: 1 found, 0 missing, -1 malformed or duplicated. */
int http_query_get(const http_request_t *request, const char *key,
                   char *value, size_t value_capacity);

int http_parse_long(const char *value, int base, long minimum, long maximum,
                    long *result);

void http_builder_init(http_builder_t *builder, char *buffer, size_t capacity);
int http_builder_append(http_builder_t *builder, const char *text);
int http_builder_appendf(http_builder_t *builder, const char *format, ...);
int http_builder_append_json_string(http_builder_t *builder, const char *text);

int http_constant_time_equal(const char *left, const char *right);
int http_send_all(int fd, const void *data, size_t length);

#endif
