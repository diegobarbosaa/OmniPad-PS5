#include "tcp_stream.h"
#include "ps5_vpad.h"
#include "pad_types.h"
#include "log.h"
#include "util.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>

static int g_stream_fd = -1;
static int g_stream_slot = -1;

int tcp_stream_init(int port)
{
    g_stream_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_stream_fd < 0) return 0;

    int opt = 1;
    setsockopt(g_stream_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((uint16_t)port);
    sin.sin_addr.s_addr = INADDR_ANY;

    if (bind(g_stream_fd, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
        close(g_stream_fd);
        g_stream_fd = -1;
        return 0;
    }

    if (listen(g_stream_fd, 2) != 0) {
        close(g_stream_fd);
        g_stream_fd = -1;
        return 0;
    }

    fcntl(g_stream_fd, F_SETFL, fcntl(g_stream_fd, F_GETFL) | O_NONBLOCK);
    log_line("tcp_stream: Test frame stream listening on port %d", port);
    return 1;
}

void tcp_stream_poll(long now)
{
    (void)now;
    if (g_stream_fd < 0) return;

    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int cfd = accept(g_stream_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (cfd < 0) return;

    /* 16-byte test frame format:
     * uint32_t buttons (little endian)
     * uint8_t lx, ly, rx, ry
     * uint8_t l2, r2
     * 6 bytes unused */
    uint8_t frame[16];
    ssize_t n = read(cfd, frame, sizeof(frame));
    if (n >= 10) {
        if (g_stream_slot < 0) {
            /* Try to allocate slot 0 for stream testing */
            g_stream_slot = 0;
            if (!vpad_is_live(0)) {
                vpad_add(0, CONN_NETWORK_STREAM, "LAN Debug Pad");
            }
        }

        pad_state_t st;
        pad_state_neutral(&st);
        st.buttons = (uint32_t)(frame[0] | (frame[1] << 8) | (frame[2] << 16) | (frame[3] << 24));
        st.lx = frame[4];
        st.ly = frame[5];
        st.rx = frame[6];
        st.ry = frame[7];
        st.l2 = frame[8];
        st.r2 = frame[9];
        st.conn_type = CONN_NETWORK_STREAM;

        vpad_update(g_stream_slot, &st);
        log_line("tcp_stream: Injected frame: buttons=0x%08x, lx=%d, ly=%d",
                 st.buttons, st.lx, st.ly);
    }
    close(cfd);
}

void tcp_stream_cleanup(void)
{
    if (g_stream_fd >= 0) {
        close(g_stream_fd);
        g_stream_fd = -1;
    }
}
