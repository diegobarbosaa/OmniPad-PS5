#include "tcp_stream.h"

#include "log.h"
#include "ps5_vpad.h"
#include "tcp_frames.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

static int g_stream_fd = -1;
#ifdef OMNIPAD_ENABLE_TCP_DEBUG
static int g_client_fd = -1;
static int g_stream_slot = -1;
static tcp_frame_buffer_t g_frame_buffer;
static long g_client_last_activity;
#endif

#ifdef OMNIPAD_ENABLE_TCP_DEBUG
static int apply_debug_frame(const pad_state_t *state, void *context)
{
    int slot = *(const int *)context;
    if (slot < 0 || slot >= MAX_SLOTS || !state) return 0;
    vpad_update(slot, state);
    return 1;
}

static void release_debug_client(void)
{
    if (g_client_fd >= 0) {
        close(g_client_fd);
        g_client_fd = -1;
    }
    if (g_stream_slot >= 0 && g_stream_slot < MAX_SLOTS) {
        vpad_remove(g_stream_slot);
        g_stream_slot = -1;
    }
    memset(&g_frame_buffer, 0, sizeof(g_frame_buffer));
}

static int accept_debug_client(long now)
{
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int cfd = accept(g_stream_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (cfd < 0) return 0;

    int slot = -1;
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (vpad_add(i, CONN_NETWORK_STREAM, "Local Debug Pad")) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        close(cfd);
        log_line("tcp_stream: debug client rejected; no free virtual slot");
        return 0;
    }

    int flags = fcntl(cfd, F_GETFL, 0);
    if (flags < 0 || fcntl(cfd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(cfd);
        vpad_remove(slot);
        return 0;
    }

    g_client_fd = cfd;
    g_stream_slot = slot;
    g_client_last_activity = now;
    memset(&g_frame_buffer, 0, sizeof(g_frame_buffer));
    log_line("tcp_stream: local debug client owns virtual slot %d", slot);
    return 1;
}
#endif

int tcp_stream_init(int port)
{
#ifndef OMNIPAD_ENABLE_TCP_DEBUG
    (void)port;
    log_line("tcp_stream: disabled (rebuild with TCP_DEBUG=1 for local development)");
    return 0;
#else
    if (g_stream_fd >= 0) return 1;
    if (port <= 0 || port > 65535) return 0;

    g_stream_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_stream_fd < 0) return 0;

    int opt = 1;
    setsockopt(g_stream_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((uint16_t)port);
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(g_stream_fd, (struct sockaddr *)&sin, sizeof(sin)) != 0 ||
        listen(g_stream_fd, 2) != 0) {
        close(g_stream_fd);
        g_stream_fd = -1;
        return 0;
    }

    int flags = fcntl(g_stream_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(g_stream_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(g_stream_fd);
        g_stream_fd = -1;
        return 0;
    }
    log_line("tcp_stream: local development input enabled on 127.0.0.1:%d", port);
    return 1;
#endif
}

void tcp_stream_poll(long now)
{
#ifdef OMNIPAD_ENABLE_TCP_DEBUG
    if (g_stream_fd < 0) return;
    if (g_client_fd < 0 && !accept_debug_client(now)) return;
    if (now - g_client_last_activity > 5000L) {
        log_line("tcp_stream: idle debug client expired");
        release_debug_client();
        return;
    }

    uint8_t input[64];
    size_t budget = 256u;
    while (budget > 0) {
        size_t request_len = budget < sizeof(input) ? budget : sizeof(input);
        ssize_t n = read(g_client_fd, input, request_len);
        if (n > 0) {
            g_client_last_activity = now;
            budget -= (size_t)n;
            if (!tcp_stream_feed(&g_frame_buffer, input, (size_t)n,
                                 apply_debug_frame, &g_stream_slot)) {
                log_line("tcp_stream: invalid debug frame; closing client");
                release_debug_client();
                return;
            }
            continue;
        }
        if (n == 0) {
            if (g_frame_buffer.used != 0) {
                log_line("tcp_stream: incomplete debug frame; closing client");
            }
            release_debug_client();
            return;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        log_line("tcp_stream: client read failed (errno=%d)", errno);
        release_debug_client();
        return;
    }
#else
    (void)now;
#endif
}

void tcp_stream_cleanup(void)
{
#ifdef OMNIPAD_ENABLE_TCP_DEBUG
    release_debug_client();
#endif
    if (g_stream_fd >= 0) {
        close(g_stream_fd);
        g_stream_fd = -1;
    }
}

#if defined(OMNIPAD_ENABLE_TCP_DEBUG) && defined(TCP_STREAM_TESTING)
int tcp_stream_test_port(void)
{
    struct sockaddr_in address;
    socklen_t length = sizeof(address);
    if (g_stream_fd < 0 || getsockname(g_stream_fd, (struct sockaddr *)&address, &length) != 0) return -1;
    return (int)ntohs(address.sin_port);
}
#endif
