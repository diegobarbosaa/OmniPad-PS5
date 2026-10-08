#include "../src/http_core.h"

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void test_request_and_query_parsing(void)
{
    const char request_text[] =
        "POST /api/rebind?slot=2&user=0x10000002 HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Authorization: Bearer abcdef0123456789\r\n"
        "Content-Length: 0\r\n\r\n";
    http_request_t request;
    char value[32];
    long parsed;

    assert(http_header_length(request_text, sizeof(request_text) - 1u) == sizeof(request_text) - 1u);
    assert(http_request_parse(request_text, sizeof(request_text) - 1u, &request));
    assert(strcmp(request.method, "POST") == 0);
    assert(strcmp(request.path, "/api/rebind") == 0);
    assert(request.has_authorization);
    assert(strcmp(request.authorization, "abcdef0123456789") == 0);
    assert(request.has_host && strcmp(request.host, "localhost") == 0);
    assert(!request.has_origin);
    assert(http_query_get(&request, "slot", value, sizeof(value)) == 1);
    assert(strcmp(value, "2") == 0);
    assert(http_query_get(&request, "user", value, sizeof(value)) == 1);
    assert(http_parse_long(value, 0, 0, INT32_MAX, &parsed));
    assert(parsed == 0x10000002L);
    assert(http_query_get(&request, "missing", value, sizeof(value)) == 0);

    const char duplicate[] = "GET /?slot=1&slot=2 HTTP/1.1\r\n\r\n";
    assert(http_request_parse(duplicate, sizeof(duplicate) - 1u, &request));
    assert(http_query_get(&request, "slot", value, sizeof(value)) == -1);
    const char encoded[] = "GET /?name=a%20b+c HTTP/1.1\r\n\r\n";
    assert(http_request_parse(encoded, sizeof(encoded) - 1u, &request));
    assert(http_query_get(&request, "name", value, sizeof(value)) == 1);
    assert(strcmp(value, "a b c") == 0);
    const char bad_escape[] = "GET /?slot=%Q0 HTTP/1.1\r\n\r\n";
    assert(http_request_parse(bad_escape, sizeof(bad_escape) - 1u, &request));
    assert(http_query_get(&request, "slot", value, sizeof(value)) == -1);
}

static void test_malformed_requests(void)
{
    http_request_t request;
    const char *bad[] = {
        "GET /api/status\r\n\r\n",
        "GET  / HTTP/1.1\r\n\r\n",
        "GET / HTTP/2.0\r\n\r\n",
        "GET / HTTP/1.1\r\nBroken\r\n\r\n",
        "GET / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n",
        "GET / HTTP/1.1\r\nAuthorization: Bearer x\r\nAuthorization: Bearer y\r\n\r\n",
        "GET / HTTP/1.1\r\nContent-Length: 999999999999999999999999\r\n\r\n"
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        assert(!http_request_parse(bad[i], strlen(bad[i]), &request));
    }
    const char partial[] = "GET /api/status HTTP/1.1\r\nHost:";
    assert(http_header_length(partial, sizeof(partial) - 1u) == 0);
    assert(!http_request_parse(partial, sizeof(partial) - 1u, &request));

    assert(!http_parse_long("-1", 10, 0, 2, &(long){0}));
    assert(!http_parse_long("999999999999999999999999", 10, 0, 4, &(long){0}));
    assert(!http_parse_long("1x", 10, 0, 4, &(long){0}));
}

static void test_bounded_json_builder(void)
{
    char buffer[128];
    char tiny[5];
    char invalid_utf8[] = {(char)0xc0, 'x', '\0'};
    http_builder_t builder;

    http_builder_init(&builder, buffer, sizeof(buffer));
    assert(http_builder_append(&builder, "{\"name\":"));
    assert(http_builder_append_json_string(&builder, "quote\" slash\\ newline\n tab\t control\001 and \xc3\xa9"));
    assert(http_builder_append(&builder, "}"));
    assert(strstr(buffer, "\\\""));
    assert(strstr(buffer, "\\\\"));
    assert(strstr(buffer, "\\n"));
    assert(strstr(buffer, "\\t"));
    assert(strstr(buffer, "\\u0001"));
    assert(strstr(buffer, "\xc3\xa9"));
    assert(builder.length == strlen(buffer));

    http_builder_init(&builder, tiny, sizeof(tiny));
    assert(!http_builder_append(&builder, "12345"));
    assert(builder.failed);
    assert(tiny[sizeof(tiny) - 1u] == '\0');

    http_builder_init(&builder, buffer, sizeof(buffer));
    assert(http_builder_append_json_string(&builder, invalid_utf8));
    assert(strcmp(buffer, "\"\\u00c0x\"") == 0);
}

static const char *send_expected;
static size_t send_expected_len;

static void *read_socket(void *argument)
{
    int fd = *(int *)argument;
    char chunk[257];
    size_t offset = 0;
    while (offset < send_expected_len) {
        ssize_t count = recv(fd, chunk, sizeof(chunk), 0);
        assert(count > 0);
        for (ssize_t i = 0; i < count; i++) {
            assert(chunk[i] == send_expected[offset + (size_t)i]);
        }
        offset += (size_t)count;
    }
    return NULL;
}

static void test_partial_socket_writes(void)
{
    int sockets[2];
    int send_buffer = 256;
    pthread_t reader;
    char *message = (char *)malloc(1024u * 1024u);
    assert(message);
    for (size_t i = 0; i < 1024u * 1024u; i++) message[i] = (char)(i % 251u);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    setsockopt(sockets[0], SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer));
    send_expected = message;
    send_expected_len = 1024u * 1024u;
    assert(pthread_create(&reader, NULL, read_socket, &sockets[1]) == 0);
    assert(http_send_all(sockets[0], message, send_expected_len));
    shutdown(sockets[0], SHUT_WR);
    assert(pthread_join(reader, NULL) == 0);
    close(sockets[0]);
    close(sockets[1]);
    free(message);
}

int main(void)
{
    test_request_and_query_parsing();
    test_malformed_requests();
    test_bounded_json_builder();
    test_partial_socket_writes();
    puts("HTTP parser, JSON builder, and partial-write tests passed.");
    return 0;
}
