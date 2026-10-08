#define _DEFAULT_SOURCE
#include "../src/web.h"
#include "../src/ps5_vpad.h"
#include "../src/shellui_inject.h"
#include "../src/http_core.h"

#include <assert.h>
#include <stdarg.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/stat.h>

#ifndef TEST_WEB_TOKEN_PATH
#define TEST_WEB_TOKEN_PATH "build/test_web.token"
#endif
#ifndef TEST_WEB_STOP_FLAG
#define TEST_WEB_STOP_FLAG "build/test_web.stop"
#endif

static vpad_slot_info_t slot_info[MAX_SLOTS];
static int slot_live[MAX_SLOTS];
static int remove_calls;
static int press_calls;
static int rebind_calls;
static int update_calls;

void log_line(const char *format, ...)
{
    (void)format;
}

long now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000L + tv.tv_usec / 1000L;
}

void vpad_get_slot_info(int slot, vpad_slot_info_t *out)
{
    if (slot >= 0 && slot < MAX_SLOTS && out) *out = slot_info[slot];
}

int vpad_is_live(int slot)
{
    return slot >= 0 && slot < MAX_SLOTS ? slot_live[slot] : 0;
}

void vpad_press_ps_button(int slot)
{
    if (slot >= 0 && slot < MAX_SLOTS) press_calls++;
}

void vpad_remove(int slot)
{
    if (slot >= 0 && slot < MAX_SLOTS) remove_calls++;
}

int vpad_rebind_user(int slot, int32_t user_id)
{
    (void)slot; (void)user_id;
    rebind_calls++;
    return 1;
}

void vpad_update(int slot, const pad_state_t *state)
{
    (void)slot; (void)state;
    update_calls++;
}

int shellui_press_ps_button(int32_t handle)
{
    (void)handle;
    return 1;
}

static int test_port(void)
{
    int port = web_test_bound_port();
    assert(port > 0 && port <= 65535);
    return port;
}

typedef struct {
    int fd;
    const char *request;
    size_t length;
    size_t split;
} split_sender_t;

static void *send_split_request(void *argument)
{
    split_sender_t *sender = (split_sender_t *)argument;
    assert(send(sender->fd, sender->request, sender->split, 0) == (ssize_t)sender->split);
    usleep(15000);
    size_t rest = sender->length - sender->split;
    assert(send(sender->fd, sender->request + sender->split, rest, 0) == (ssize_t)rest);
    return NULL;
}

static char *round_trip(const char *request, int split)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server;
    assert(fd >= 0);
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons((uint16_t)test_port());
    server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(connect(fd, (struct sockaddr *)&server, sizeof(server)) == 0);
    struct timeval recv_timeout = {2, 0};
    assert(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                      &recv_timeout, sizeof(recv_timeout)) == 0);

    size_t request_len = strlen(request);
    pthread_t sender_thread;
    split_sender_t sender = {fd, request, request_len,
                             split ? request_len / 2u : request_len};
    if (split) {
        assert(pthread_create(&sender_thread, NULL, send_split_request, &sender) == 0);
    } else {
        assert(send(fd, request, request_len, 0) == (ssize_t)request_len);
    }

    int elapsed_ms = 0;
    struct pollfd response_ready = {fd, POLLIN, 0};
    while (elapsed_ms < 2000) {
        web_poll(now_ms());
        int ready = poll(&response_ready, 1, 10);
        if (ready > 0) break;
        if (ready < 0 && errno != EINTR) abort();
        elapsed_ms += 10;
    }
    assert(elapsed_ms < 2000);
    if (split) assert(pthread_join(sender_thread, NULL) == 0);

    char *response = (char *)malloc(16384u);
    assert(response);
    size_t used = 0;
    for (;;) {
        ssize_t count = recv(fd, response + used, 16383u - used, 0);
        if (count == 0) break;
        if (count < 0) {
            const char *line_end = strstr(request, "\r\n");
            fprintf(stderr, "Timed out receiving response for %.*s (errno=%d)\n",
                    line_end ? (int)(line_end - request) : (int)strlen(request),
                    request, errno);
        }
        assert(count > 0 && used + (size_t)count < 16384u);
        used += (size_t)count;
    }
    response[used] = '\0';
    close(fd);
    return response;
}

static int response_status(const char *response)
{
    int status = 0;
    assert(sscanf(response, "HTTP/1.1 %d", &status) == 1);
    return status;
}

static void assert_content_length_matches(const char *response)
{
    const char *length_header = strstr(response, "Content-Length: ");
    const char *body = strstr(response, "\r\n\r\n");
    assert(length_header && body);
    size_t declared = (size_t)strtoul(length_header + strlen("Content-Length: "), NULL, 10);
    body += 4;
    assert(strlen(body) == declared);
}

static void test_dashboard_and_local_mode(void)
{
    char long_name[64];
    memset(slot_info, 0, sizeof(slot_info));
    memset(slot_live, 0, sizeof(slot_live));
    slot_info[0].status = VP_READY;
    slot_info[0].handle = 1;
    snprintf(slot_info[0].name, sizeof(slot_info[0].name), "Quote\" slash\\ line\n tab\t ctl\001 \xc3\xa9");
    memset(long_name, 'L', sizeof(long_name) - 1u);
    long_name[sizeof(long_name) - 1u] = '\0';
    slot_info[1].status = VP_QUEUED;
    memcpy(slot_info[1].name, long_name, sizeof(long_name));
    slot_info[2].status = VP_FREE;
    strcpy(slot_info[2].name, "玩家");
    slot_live[0] = 1;

    assert(unlink(TEST_WEB_TOKEN_PATH) == 0 || access(TEST_WEB_TOKEN_PATH, F_OK) != 0);
    if (!web_init(0)) { fprintf(stderr, "web_init failed, errno=%d\n", errno); abort(); }
    assert(!web_test_lan_auth_enabled());
    char *response = round_trip("GET /api/status HTTP/1.1\r\nHost: localhost\r\n\r\n", 0);
    assert(response_status(response) == 200);
    assert_content_length_matches(response);
    assert(strstr(response, "Quote\\\" slash\\\\ line\\n tab\\t ctl\\u0001 \xc3\xa9"));
    assert(strstr(response, long_name));
    assert(strstr(response, "玩家"));
    assert(strstr(response, "\"status\":0,"));
    assert(strstr(response, "X-Frame-Options: DENY"));
    assert(!strstr(response, "Access-Control-Allow-Origin"));
    free(response);

    response = round_trip("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n", 0);
    assert(response_status(response) == 200);
    assert(strstr(response, "LAN access token"));
    assert_content_length_matches(response);
    free(response);

    char local_request[256];
    int port = test_port();
    snprintf(local_request, sizeof(local_request),
        "POST /api/press_ps?slot=0 HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nOrigin: https://attacker.invalid\r\n\r\n", port);
    response = round_trip(local_request, 0);
    assert(response_status(response) == 403);
    assert(press_calls == 0);
    free(response);

    snprintf(local_request, sizeof(local_request),
        "POST /api/press_ps?slot=0 HTTP/1.1\r\nHost: attacker.invalid:%d\r\nOrigin: http://attacker.invalid:%d\r\n\r\n", port, port);
    response = round_trip(local_request, 0);
    assert(response_status(response) == 403); /* reject DNS-rebinding style hostnames */
    assert(press_calls == 0);
    free(response);

    snprintf(local_request, sizeof(local_request),
        "GET /api/status HTTP/1.1\r\nHost: attacker.invalid:%d\r\nOrigin: http://attacker.invalid:%d\r\n\r\n", port, port);
    response = round_trip(local_request, 0);
    assert(response_status(response) == 403);
    free(response);

    snprintf(local_request, sizeof(local_request),
        "POST /api/press_ps?slot=0 HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nOrigin: http://127.0.0.1:%d\r\n\r\n", port, port);
    response = round_trip(local_request, 0);
    assert(response_status(response) == 200);
    assert(press_calls == 1);
    free(response);

    response = round_trip("POST /api/disconnect?slot=-1 HTTP/1.1\r\nHost: localhost\r\n\r\n", 0);
    assert(response_status(response) == 200);
    assert(remove_calls == MAX_SLOTS);
    free(response);
    web_cleanup();
}

static void write_test_token(const char *token)
{
    FILE *file = fopen(TEST_WEB_TOKEN_PATH, "w");
    assert(file);
    assert(fputs(token, file) >= 0);
    assert(fclose(file) == 0);
    assert(chmod(TEST_WEB_TOKEN_PATH, 0600) == 0);
}

static void test_authenticated_lan_mode(void)
{
    const char token[] = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    char request[512];
    char large_auth[HTTP_AUTH_MAX + 2u];
    char *response;
    int before = remove_calls;
    int before_press = press_calls;
    int before_rebind = rebind_calls;
    int before_update = update_calls;
    const char *unauthorized_requests[] = {
        "POST /api/pair HTTP/1.1\r\nHost: localhost\r\n\r\n",
        "POST /api/press_ps?slot=0 HTTP/1.1\r\nHost: localhost\r\n\r\n",
        "POST /api/disconnect?slot=-1 HTTP/1.1\r\nHost: localhost\r\n\r\n",
        "POST /api/rebind?slot=0&user=-1 HTTP/1.1\r\nHost: localhost\r\n\r\n",
        "POST /api/exit HTTP/1.1\r\nHost: localhost\r\n\r\n",
        "POST /api/send_btn?slot=0&btn=cross HTTP/1.1\r\nHost: localhost\r\n\r\n"
    };

    write_test_token(token);
    assert(web_init(0));
    assert(web_test_lan_auth_enabled());

    unlink(TEST_WEB_STOP_FLAG);
    for (size_t i = 0; i < sizeof(unauthorized_requests) / sizeof(unauthorized_requests[0]); i++) {
        response = round_trip(unauthorized_requests[i], 0);
        assert(response_status(response) == 401);
        free(response);
    }
    assert(remove_calls == before);
    assert(press_calls == before_press);
    assert(rebind_calls == before_rebind);
    assert(update_calls == before_update);
    assert(access(TEST_WEB_STOP_FLAG, F_OK) != 0);

    char wrong_token[sizeof(token)];
    memset(wrong_token, 'x', sizeof(wrong_token) - 1u);
    wrong_token[sizeof(wrong_token) - 1u] = '\0';
    snprintf(request, sizeof(request),
        "POST /api/disconnect?slot=0 HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer %s\r\n\r\n",
        wrong_token);
    response = round_trip(request, 0);
    assert(response_status(response) == 401);
    assert(!strstr(response, token));
    assert(remove_calls == before);
    free(response);

    snprintf(request, sizeof(request),
        "POST /api/pair HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer %s\r\n\r\n", token);
    response = round_trip(request, 0);
    assert(response_status(response) == 200);
    assert(!strstr(response, token));
    free(response);

    snprintf(request, sizeof(request),
        "POST /api/press_ps?slot=0 HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer %s\r\n\r\n", token);
    response = round_trip(request, 0);
    assert(response_status(response) == 200);
    assert(press_calls == before_press + 1);
    free(response);

    snprintf(request, sizeof(request),
        "POST /api/rebind?slot=0&user=-1 HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer %s\r\n\r\n", token);
    response = round_trip(request, 0);
    assert(response_status(response) == 200);
    assert(rebind_calls == before_rebind + 1);
    free(response);

    snprintf(request, sizeof(request),
        "POST /api/send_btn?slot=0&btn=cross HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer %s\r\n\r\n", token);
    response = round_trip(request, 0);
    assert(response_status(response) == 200);
    assert(update_calls == before_update + 2);
    free(response);

    snprintf(request, sizeof(request),
        "POST /api/exit HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer %s\r\n\r\n", token);
    response = round_trip(request, 0);
    assert(response_status(response) == 200);
    assert(access(TEST_WEB_STOP_FLAG, F_OK) == 0);
    free(response);
    unlink(TEST_WEB_STOP_FLAG);

    snprintf(request, sizeof(request),
        "POST /api/disconnect?slot=-1 HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer %s\r\n\r\n", token);
    response = round_trip(request, 1);
    assert(response_status(response) == 200);
    assert(remove_calls == before + MAX_SLOTS);
    free(response);

    response = round_trip("GET /api/status HTTP/1.1\r\nHost: localhost\r\n\r\n", 0);
    assert(response_status(response) == 200); /* explicitly public read-only status */
    free(response);
    response = round_trip("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n", 0);
    assert(response_status(response) == 200); /* dashboard remains available in LAN mode */
    assert(strstr(response, "LAN access token"));
    free(response);
    response = round_trip("GET /api/log HTTP/1.1\r\nHost: localhost\r\n\r\n", 0);
    assert(response_status(response) == 401); /* diagnostic logs require LAN auth */
    free(response);

    snprintf(request, sizeof(request),
        "POST /api/disconnect?slot=0 HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nOrigin: http://127.0.0.1:%d\r\n\r\n",
        test_port(), test_port());
    response = round_trip(request, 0);
    assert(response_status(response) == 401); /* same-origin is not LAN authorization */
    assert(remove_calls == before + MAX_SLOTS);
    free(response);

    snprintf(request, sizeof(request),
        "POST /api/disconnect HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer %s\r\n\r\n", token);
    response = round_trip(request, 0);
    assert(response_status(response) == 400);
    free(response);

    snprintf(request, sizeof(request),
        "POST /api/disconnect?slot=-2 HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer %s\r\n\r\n", token);
    response = round_trip(request, 0);
    assert(response_status(response) == 400);
    free(response);

    snprintf(request, sizeof(request),
        "POST /api/disconnect?slot=999999999999999999999 HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer %s\r\n\r\n", token);
    response = round_trip(request, 0);
    assert(response_status(response) == 400);
    free(response);

    snprintf(request, sizeof(request),
        "GET /api/disconnect?slot=0 HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer %s\r\n\r\n", token);
    response = round_trip(request, 0);
    assert(response_status(response) == 405);
    free(response);

    snprintf(request, sizeof(request),
        "OPTIONS /api/disconnect HTTP/1.1\r\nHost: localhost\r\nOrigin: https://attacker.invalid\r\n\r\n");
    response = round_trip(request, 0);
    assert(response_status(response) == 405);
    assert(!strstr(response, "Access-Control-Allow-Origin"));
    free(response);

    memset(large_auth, 'a', sizeof(large_auth) - 1u);
    large_auth[sizeof(large_auth) - 1u] = '\0';
    snprintf(request, sizeof(request),
        "POST /api/disconnect?slot=0 HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer %s\r\n\r\n", large_auth);
    response = round_trip(request, 0);
    assert(response_status(response) == 400);
    assert(remove_calls == before + MAX_SLOTS);
    free(response);

    response = round_trip("GET /api/status HTTP/1.1\r\nHost: localhost\r\n", 0);
    assert(response_status(response) == 400 || response_status(response) == 408);
    free(response);

    response = round_trip("GET /api/status HTTP/1.1\r\nHost:", 0);
    assert(response_status(response) == 400 || response_status(response) == 408);
    free(response);

    web_cleanup();
    unlink(TEST_WEB_TOKEN_PATH);
}

static void test_insecure_token_permissions_do_not_enable_lan(void)
{
    const char token[] = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    write_test_token(token);
    assert(chmod(TEST_WEB_TOKEN_PATH, 0644) == 0);
    assert(web_init(0));
    assert(!web_test_lan_auth_enabled());
    web_cleanup();
    assert(unlink(TEST_WEB_TOKEN_PATH) == 0);
}

int main(void)
{
    test_dashboard_and_local_mode();
    test_insecure_token_permissions_do_not_enable_lan();
    test_authenticated_lan_mode();
    puts("Web authorization, parsing, JSON, and dashboard tests passed.");
    return 0;
}
