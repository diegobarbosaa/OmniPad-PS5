#include "web.h"
#include "ps5_vpad.h"
#include "shellui_inject.h"
#include "bt_host.h"
#include "usb_hotplug.h"
#include "version.h"
#include "log.h"
#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

static int g_server_fd = -1;

/* The dashboard (web/index.html) is embedded at build time by
 * tools/embed_html.py into src/web_page.c. One page serves everything:
 * controller slots, Bluetooth pairing, button mappings and settings. */
extern const unsigned char web_page_html[];
extern const unsigned int  web_page_html_len;
typedef struct { const char *name, *mime; const unsigned char *data; unsigned int len; } web_asset_t;
extern const web_asset_t web_assets[];

/* GET /img/<name>: an embedded image (web/img/). Returns 0 if unknown. */
static int send_asset(int cfd, const char *req)
{
    const char *p = req + 9;                      /* after "GET /img/" */
    size_t n = 0;
    while (p[n] && p[n] != ' ' && p[n] != '?') n++;
    for (const web_asset_t *a = web_assets; a->name; a++) {
        if (strlen(a->name) != n || strncmp(a->name, p, n) != 0) continue;
        char hdr[256];
        int hl = snprintf(hdr, sizeof hdr,
            "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n"
            "Cache-Control: public, max-age=86400\r\nContent-Length: %u\r\n"
            "Connection: close\r\n\r\n", a->mime, a->len);
        write(cfd, hdr, (size_t)hl);
        size_t off = 0;
        while (off < a->len) {
            ssize_t w = write(cfd, a->data + off, a->len - off);
            if (w <= 0) break;
            off += (size_t)w;
        }
        return 1;
    }
    return 0;
}

static void send_page(int cfd)
{
    char hdr[256];
    int hl = snprintf(hdr, sizeof hdr,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Cache-Control: no-cache, no-store, must-revalidate\r\n"
        "Pragma: no-cache\r\n"
        "Expires: 0\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n\r\n", web_page_html_len);
    write(cfd, hdr, (size_t)hl);
    /* Chunked write to tolerate short write() returns on slow clients. */
    size_t off = 0;
    while (off < web_page_html_len) {
        ssize_t w = write(cfd, web_page_html + off, web_page_html_len - off);
        if (w <= 0) break;
        off += (size_t)w;
    }
}

/* Parses ?mac=AA:BB:CC:DD:EE:FF from a request line (also AA-BB-.., AABB..,
 * and %3A-encoded colons) into controller byte order (mac[0] = last octet).
 * Returns 1 on success. */
static int parse_mac_param(const char *req, unsigned char mac[6])
{
    const char *p = strstr(req, "mac=");
    unsigned v[6];
    int k = 0;
    if (!p) return 0;
    p += 4;
    while (k < 6) {
        int hi, lo;
        if (!isxdigit((unsigned char)p[0]) || !isxdigit((unsigned char)p[1])) return 0;
        hi = isdigit((unsigned char)p[0]) ? p[0] - '0' : (tolower((unsigned char)p[0]) - 'a' + 10);
        lo = isdigit((unsigned char)p[1]) ? p[1] - '0' : (tolower((unsigned char)p[1]) - 'a' + 10);
        v[k++] = (unsigned)(hi << 4 | lo);
        p += 2;
        if (k == 6) break;
        if (*p == ':' || *p == '-') p++;
        else if (p[0] == '%' && p[1] == '3' && (p[2] == 'A' || p[2] == 'a')) p += 3;
    }
    for (int i = 0; i < 6; i++) mac[i] = (unsigned char)v[5 - i];
    return 1;
}

static void send_json(int cfd, int code, const char *body)
{
    char hdr[256];
    int hl = snprintf(hdr, sizeof hdr,
        "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nContent-Length: %zu\r\n"
        "Connection: close\r\n\r\n", code, code == 200 ? "OK" : code == 404 ? "Not Found" : "Error", strlen(body));
    write(cfd, hdr, (size_t)hl);
    write(cfd, body, strlen(body));
}

#ifndef SO_REUSEPORT
#define SO_REUSEPORT 0x0200
#endif

int web_init(int port)
{
    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) return 0;

    int opt = 1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((uint16_t)port);
    sin.sin_addr.s_addr = INADDR_ANY;

    int bound = 0;
    for (int retry = 0; retry < 10; retry++) {
        if (bind(g_server_fd, (struct sockaddr *)&sin, sizeof(sin)) == 0) {
            bound = 1;
            break;
        }
        if (retry == 0) {
            log_line("web: port %d busy, waiting for release...", port);
        }
        usleep(250000); /* 250ms */
    }

    if (!bound) {
        log_line("web: bind error on port %d (%s)", port, strerror(errno));
        close(g_server_fd);
        g_server_fd = -1;
        return 0;
    }

    if (listen(g_server_fd, 8) != 0) {
        close(g_server_fd);
        g_server_fd = -1;
        return 0;
    }

    fcntl(g_server_fd, F_SETFL, fcntl(g_server_fd, F_GETFL) | O_NONBLOCK);
    log_line("web: Dashboard server listening at http://0.0.0.0:%d/", port);
    return 1;
}

static void handle_client(int cfd)
{
    /* Clear O_NONBLOCK inherited from listening socket */
    int flags = fcntl(cfd, F_GETFL, 0);
    if (flags >= 0) fcntl(cfd, F_SETFL, flags & ~O_NONBLOCK);

    /* Set 100ms socket read timeout to prevent stalling the main 250Hz loop */
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000; /* 100 ms */
    setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(cfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct pollfd pfd;
    pfd.fd = cfd;
    pfd.events = POLLIN;
    int pr = poll(&pfd, 1, 100);
    if (pr <= 0) { close(cfd); return; }

    char req[4096];
    ssize_t n = read(cfd, req, sizeof(req) - 1);
    if (n <= 0) { close(cfd); return; }
    req[n] = '\0';
    /* A POST body may come in a later packet than the headers: keep reading
     * (up to 150 ms; this runs on the main loop) until Content-Length bytes are in. */
    {
        char *hdr_end = strstr(req, "\r\n\r\n");
        const char *cl = strstr(req, "Content-Length:");
        if (!cl) cl = strstr(req, "content-length:");
        if (hdr_end && cl && cl < hdr_end) {
            long want = atol(cl + 15);
            long have = (long)(req + n - (hdr_end + 4));
            while (want > have && n < (ssize_t)sizeof(req) - 1) {
                struct pollfd more = { .fd = cfd, .events = POLLIN };
                if (poll(&more, 1, 150) <= 0) break;
                ssize_t m = read(cfd, req + n, sizeof(req) - 1 - (size_t)n);
                if (m <= 0) break;
                n += m;
                have += m;
                req[n] = '\0';
            }
        }
    }

    char res[4096];
    int res_len = 0;

    if (strncmp(req, "OPTIONS", 7) == 0) {
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 204 No Content\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n\r\n");
        write(cfd, res, (size_t)res_len);
        close(cfd);
        return;
    }

    if (strncmp(req, "GET /api/status", 15) == 0) {
        char json[2048];
        int jlen = snprintf(json, sizeof(json),
            "{\"version\":\"%s\",\"build\":%d,\"slots\":[",
            ANYPAD_VERSION, ANYPAD_BUILD);

        for (int i = 0; i < MAX_SLOTS; i++) {
            vpad_slot_info_t info;
            vpad_get_slot_info(i, &info);
            /* Bluetooth pads also report who they are, so the page can show
             * the right picture and that controller's own settings. */
            uint16_t vid = 0, pid = 0;
            unsigned char mac[6] = {0};
            char macs[18] = "", sid[20] = "";
            int is_usb = info.conn_type == CONN_USB_WIRED || info.conn_type == CONN_USB_DONGLE_24G;
            if (info.status == VP_READY && !is_usb && bt_host_slot_ids(i, &vid, &pid, mac)) {
                snprintf(macs, sizeof macs, "%02X:%02X:%02X:%02X:%02X:%02X", mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
                snprintf(sid, sizeof sid, "%02X%02X%02X%02X%02X%02X", mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
            } else if (info.status == VP_READY && is_usb && usb_hotplug_slot_ids(i, &vid, &pid)) {
                /* USB pads have no unique id: settings are per model. */
                snprintf(sid, sizeof sid, "USB_%04X_%04X", vid, pid);
            }
            jlen += snprintf(json + jlen, sizeof(json) - (size_t)jlen,
                "%s{\"status\":%d,\"name\":\"%s\",\"conn\":%d,\"handle\":%d,\"alt_handle\":%d,\"user_id\":%u,\"battery\":%d,\"charging\":%d,\"injected\":%u,\"vid\":%u,\"pid\":%u,\"mac\":\"%s\",\"id\":\"%s\"}",
                i > 0 ? "," : "",
                info.status, info.name, info.conn_type, info.handle, info.alt_handle,
                (unsigned)info.user_id, info.battery_level, info.battery_charging,
                (unsigned)info.packets_injected, vid, pid, macs, sid);
        }
        snprintf(json + jlen, sizeof(json) - (size_t)jlen, "]}");

        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n\r\n%s",
            strlen(json), json);
        write(cfd, res, (size_t)res_len);
    }
    else if (strncmp(req, "POST /api/pair", 14) == 0) {
        const char *ok = "{\"status\":\"usb_plug_and_play_active\"}";
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
            strlen(ok), ok);
        write(cfd, res, (size_t)res_len);
    }
    else if (strncmp(req, "POST /api/press_ps", 18) == 0) {
        char *p = strstr(req, "slot=");
        int slot = p ? atoi(p + 5) : 0;
        if (slot < 0) {
            for (int i = 0; i < MAX_SLOTS; i++) {
                if (vpad_is_live(i)) {
                    vpad_slot_info_t info;
                    vpad_get_slot_info(i, &info);
                    shellui_press_ps_button(info.handle);
                    vpad_press_ps_button(i);
                }
            }
        } else if (slot >= 0 && slot < MAX_SLOTS) {
            vpad_slot_info_t info;
            vpad_get_slot_info(slot, &info);
            shellui_press_ps_button(info.handle);
            vpad_press_ps_button(slot);
        }
        const char *ok = "{\"status\":\"ps_pressed\"}";
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
            strlen(ok), ok);
        write(cfd, res, (size_t)res_len);
    }
    else if (strncmp(req, "POST /api/disconnect", 20) == 0) {
        char *p = strstr(req, "slot=");
        int slot = p ? atoi(p + 5) : 0;
        if (slot < 0) {
            for (int i = 0; i < MAX_SLOTS; i++) vpad_remove(i);
        } else {
            vpad_remove(slot);
        }
        const char *ok = "{\"status\":\"disconnected\"}";
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
            strlen(ok), ok);
        write(cfd, res, (size_t)res_len);
    }
    else if (strncmp(req, "POST /api/rebind", 16) == 0) {
        char *p = strstr(req, "slot=");
        int slot = p ? atoi(p + 5) : 0;
        char *pu = strstr(req, "user=");
        int32_t user = pu ? (int32_t)strtol(pu + 5, NULL, 0) : -1;
        vpad_rebind_user(slot, user);
        const char *ok = "{\"status\":\"rebound\"}";
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
            strlen(ok), ok);
        write(cfd, res, (size_t)res_len);
    }
    else if (strncmp(req, "POST /api/exit", 14) == 0) {
        const char *ok = "{\"status\":\"shutting_down\"}";
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
            strlen(ok), ok);
        write(cfd, res, (size_t)res_len);
        close(cfd);
        int sfd = open("/data/anypad/stop", O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (sfd >= 0) close(sfd);
        return;
    }
    else if (strncmp(req, "GET /api/log", 12) == 0) {
        int lfd = open("/data/anypad/anypad.log", O_RDONLY);
        if (lfd < 0) lfd = open("anypad.log", O_RDONLY);
        if (lfd >= 0) {
            off_t sz = lseek(lfd, 0, SEEK_END);
            off_t start = 0;
            if (sz > 32768) start = sz - 32768;
            lseek(lfd, start, SEEK_SET);
            char logbuf[32768];
            ssize_t rb = read(lfd, logbuf, sizeof(logbuf));
            close(lfd);
            if (rb < 0) rb = 0;
            res_len = snprintf(res, sizeof(res),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain; charset=utf-8\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Content-Length: %zd\r\n"
                "Connection: close\r\n\r\n", rb);
            write(cfd, res, (size_t)res_len);
            if (rb > 0) write(cfd, logbuf, (size_t)rb);
        } else {
            const char *err = "Log file not found";
            res_len = snprintf(res, sizeof(res),
                "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
                strlen(err), err);
            write(cfd, res, (size_t)res_len);
        }
    }
    else if (strncmp(req, "POST /api/send_btn", 18) == 0) {
        char *p_slot = strstr(req, "slot=");
        int slot = p_slot ? atoi(p_slot + 5) : 0;
        char *p_code = strstr(req, "code=");
        char *p_btn = strstr(req, "btn=");
        uint32_t btn_mask = PAD_BTN_CROSS;
        if (p_code) {
            int c = atoi(p_code + 5);
            switch (c) {
            case 1: btn_mask = PAD_BTN_CROSS; break;
            case 2: btn_mask = PAD_BTN_CIRCLE; break;
            case 3: btn_mask = PAD_DPAD_UP; break;
            case 4: btn_mask = PAD_DPAD_DOWN; break;
            case 5: btn_mask = PAD_DPAD_LEFT; break;
            case 6: btn_mask = PAD_DPAD_RIGHT; break;
            case 7: btn_mask = PAD_BTN_PS; break;
            case 8: btn_mask = PAD_BTN_SQUARE; break;
            case 9: btn_mask = PAD_BTN_TRIANGLE; break;
            case 10: btn_mask = PAD_BTN_OPTIONS; break;
            case 11: btn_mask = PAD_BTN_SHARE; break;
            case 12: btn_mask = PAD_BTN_L1; break;
            case 13: btn_mask = PAD_BTN_R1; break;
            default: btn_mask = PAD_BTN_CROSS; break;
            }
        } else if (p_btn) {
            if (strncmp(p_btn + 4, "dpad_up", 7) == 0) btn_mask = PAD_DPAD_UP;
            else if (strncmp(p_btn + 4, "dpad_down", 9) == 0) btn_mask = PAD_DPAD_DOWN;
            else if (strncmp(p_btn + 4, "dpad_left", 9) == 0) btn_mask = PAD_DPAD_LEFT;
            else if (strncmp(p_btn + 4, "dpad_right", 10) == 0) btn_mask = PAD_DPAD_RIGHT;
            else if (strncmp(p_btn + 4, "circle", 6) == 0) btn_mask = PAD_BTN_CIRCLE;
            else if (strncmp(p_btn + 4, "cross", 5) == 0) btn_mask = PAD_BTN_CROSS;
            else if (strncmp(p_btn + 4, "square", 6) == 0) btn_mask = PAD_BTN_SQUARE;
            else if (strncmp(p_btn + 4, "triangle", 8) == 0) btn_mask = PAD_BTN_TRIANGLE;
            else if (strncmp(p_btn + 4, "options", 7) == 0) btn_mask = PAD_BTN_OPTIONS;
            else if (strncmp(p_btn + 4, "share", 5) == 0) btn_mask = PAD_BTN_SHARE;
            else if (strncmp(p_btn + 4, "ps", 2) == 0) btn_mask = PAD_BTN_PS;
        }

        /* If PS button is requested, also synthesize inside SceShellUI */
        if (btn_mask & PAD_BTN_PS) {
            if (slot < 0) {
                for (int i = 0; i < MAX_SLOTS; i++) {
                    if (vpad_is_live(i)) {
                        vpad_slot_info_t info;
                        vpad_get_slot_info(i, &info);
                        shellui_press_ps_button(info.handle);
                    }
                }
            } else if (slot >= 0 && slot < MAX_SLOTS && vpad_is_live(slot)) {
                vpad_slot_info_t info;
                vpad_get_slot_info(slot, &info);
                shellui_press_ps_button(info.handle);
            }
        }

        if (slot < 0) {
            for (int i = 0; i < MAX_SLOTS; i++) {
                if (vpad_is_live(i)) {
                    pad_state_t st;
                    pad_state_neutral(&st);
                    st.buttons = btn_mask;
                    vpad_update(i, &st);
                }
            }
            usleep(150000);
            for (int i = 0; i < MAX_SLOTS; i++) {
                if (vpad_is_live(i)) {
                    pad_state_t st;
                    pad_state_neutral(&st);
                    vpad_update(i, &st);
                }
            }
        } else if (slot >= 0 && slot < MAX_SLOTS && vpad_is_live(slot)) {
            pad_state_t st;
            pad_state_neutral(&st);
            st.buttons = btn_mask;
            vpad_update(slot, &st);
            usleep(150000);
            pad_state_neutral(&st);
            vpad_update(slot, &st);
        }
        const char *ok = "{\"status\":\"button_sent\"}";
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
            strlen(ok), ok);
        write(cfd, res, (size_t)res_len);
    }
    /* ---- Bluetooth control endpoints ---- */
    else if (strncmp(req, "GET /api/bt/status", 18) == 0) {
        char body[512];
        int up = bt_host_is_up();
        int left = bt_host_get_pairing_seconds_left();
        int blen = snprintf(body, sizeof body,
            "{\"up\":%s,\"pairing\":%s,\"pairing_left\":%d}",
            up ? "true":"false", left > 0 ? "true":"false", left);
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\nContent-Length: %d\r\n"
            "Connection: close\r\n\r\n%s", blen, body);
        write(cfd, res, (size_t)res_len);
    }
    else if (strncmp(req, "POST /api/bt/scan", 17) == 0) {
        /* Open the BT chip and run inquiry WITHOUT a MAC filter: on_discover
         * records every gamepad-class device it hears; dashboard reads
         * /api/bt/discovered to render the list. Still auto-pairs any pad
         * in pairing mode; the dashboard "Pair this one" flow swaps in a
         * MAC filter via /api/bt/pair?mac=... before clicking. */
        int seconds = 90;
        const char *p = strstr(req, "seconds=");
        if (p) seconds = atoi(p + 8);
        if (seconds <= 0 || seconds > 300) seconds = 90;
        /* bt_host_scan clears any MAC filter left by a scoped pairing. */
        int ok = bt_host_scan(seconds);
        const char *body = ok ? "{\"status\":\"scanning\"}" : "{\"status\":\"bt_init_failed\"}";
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 %s\r\nContent-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\nContent-Length: %zu\r\n"
            "Connection: close\r\n\r\n%s",
            ok ? "200 OK" : "503 Service Unavailable", strlen(body), body);
        write(cfd, res, (size_t)res_len);
    }
    else if (strncmp(req, "POST /api/bt/haptic", 19) == 0) {
        const char *p = strstr(req, "on=");
        int on = p && (p[3] == '1' || p[3] == 't');
        bt_haptic_enable(on);
        char body[64];
        int blen = snprintf(body, sizeof body, "{\"haptic\":%s}", on ? "true" : "false");
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\nContent-Length: %d\r\n"
            "Connection: close\r\n\r\n%s", blen, body);
        write(cfd, res, (size_t)res_len);
    }
    else if (strncmp(req, "POST /api/bt/idle_release", 25) == 0) {
        const char *p = strstr(req, "minutes=");
        int m = p ? atoi(p + 8) : 5;
        bt_host_idle_release_set(m);
        char body[64];
        int blen = snprintf(body, sizeof body, "{\"minutes\":%d}", bt_host_idle_release_get());
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\nContent-Length: %d\r\n"
            "Connection: close\r\n\r\n%s", blen, body);
        write(cfd, res, (size_t)res_len);
    }
    else if (strncmp(req, "GET /api/bt/idle_release", 24) == 0) {
        char body[64];
        int blen = snprintf(body, sizeof body, "{\"minutes\":%d}", bt_host_idle_release_get());
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\nContent-Length: %d\r\n"
            "Connection: close\r\n\r\n%s", blen, body);
        write(cfd, res, (size_t)res_len);
    }
    else if (strncmp(req, "GET /api/bt/haptic/settings", 27) == 0) {
        float gain; int eager;
        bt_haptic_settings_get(&gain, &eager);
        char body[96];
        int blen = snprintf(body, sizeof body,
            "{\"gain\":%.2f,\"eager\":%s}", gain, eager ? "true" : "false");
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\nContent-Length: %d\r\n"
            "Connection: close\r\n\r\n%s", blen, body);
        write(cfd, res, (size_t)res_len);
    }
    else if (strncmp(req, "POST /api/bt/haptic/settings", 28) == 0) {
        float gain; int eager;
        bt_haptic_settings_get(&gain, &eager);
        const char *p = strstr(req, "gain=");
        if (p) gain = (float)atof(p + 5);
        p = strstr(req, "eager=");
        if (p) eager = (p[6] == '1' || p[6] == 't');
        bt_haptic_settings_set(gain, eager);
        bt_haptic_settings_get(&gain, &eager);
        char body[96];
        int blen = snprintf(body, sizeof body,
            "{\"gain\":%.2f,\"eager\":%s}", gain, eager ? "true" : "false");
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\nContent-Length: %d\r\n"
            "Connection: close\r\n\r\n%s", blen, body);
        write(cfd, res, (size_t)res_len);
    }
    else if (strncmp(req, "GET /api/bt/haptic", 18) == 0) {
        char body[64];
        int blen = snprintf(body, sizeof body, "{\"haptic\":%s}",
                            bt_haptic_is_enabled() ? "true" : "false");
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\nContent-Length: %d\r\n"
            "Connection: close\r\n\r\n%s", blen, body);
        write(cfd, res, (size_t)res_len);
    }
    else if (strncmp(req, "GET /api/vpad/type", 18) == 0) {
        int t = vpad_get_device_type();
        char body[64];
        int blen = snprintf(body, sizeof body,
            "{\"type\":%d,\"name\":\"%s\"}",
            t, t == 2 ? "DualShock 4" : "DualSense");
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\nContent-Length: %d\r\n"
            "Connection: close\r\n\r\n%s", blen, body);
        write(cfd, res, (size_t)res_len);
    }
    else if (strncmp(req, "POST /api/vpad/type", 19) == 0) {
        const char *p = strstr(req, "type=");
        int t = p ? atoi(p + 5) : 0;
        int before = vpad_get_device_type();
        vpad_set_device_type(t);
        int after = vpad_get_device_type();
        int changed = (before != after);
        char body[128];
        int blen = snprintf(body, sizeof body,
            "{\"type\":%d,\"name\":\"%s\",\"changed\":%s,\"note\":\"applies to next virtual pad; reconnect\"}",
            after, after == 2 ? "DualShock 4" : "DualSense", changed ? "true" : "false");
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\nContent-Length: %d\r\n"
            "Connection: close\r\n\r\n%s", blen, body);
        write(cfd, res, (size_t)res_len);
    }
    else if (strncmp(req, "GET /api/bt/discovered", 22) == 0) {
        bt_discovered_t rows[16];
        int n = bt_host_discovered(rows, 16);
        char body[1024]; int blen = 0;
        blen += snprintf(body + blen, sizeof body - blen, "{\"devices\":[");
        long now = now_ms();
        for (int i = 0; i < n && blen < (int)sizeof body - 100; i++) {
            blen += snprintf(body + blen, sizeof body - blen,
                "%s{\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"cod\":%u,\"age_ms\":%ld}",
                i ? "," : "",
                rows[i].mac[5], rows[i].mac[4], rows[i].mac[3],
                rows[i].mac[2], rows[i].mac[1], rows[i].mac[0],
                rows[i].cod, now - rows[i].last_seen_ms);
        }
        blen += snprintf(body + blen, sizeof body - blen, "]}");
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\nContent-Length: %d\r\n"
            "Connection: close\r\n\r\n%s", blen, body);
        write(cfd, res, (size_t)res_len);
    }
    else if (strncmp(req, "POST /api/bt/pair", 17) == 0) {
        /* Pair a specific MAC: ?mac=AA:BB:CC:DD:EE:FF&seconds=120.
         * Without ?mac, falls back to a wide scan window (backwards-
         * compatible with the earlier UI). */
        int seconds = 120;
        unsigned char mac[6] = {0};
        int have_mac = 0;
        const char *p = strstr(req, "seconds=");
        if (p) seconds = atoi(p + 8);
        if (seconds <= 0 || seconds > 300) seconds = 120;
        have_mac = parse_mac_param(req, mac);
        int ok = have_mac ? bt_host_pair_mac(mac, seconds) : bt_host_scan(seconds);
        const char *body = ok ? "{\"status\":\"pairing_started\"}" : "{\"status\":\"bt_init_failed\"}";
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 %s\r\nContent-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\nContent-Length: %zu\r\n"
            "Connection: close\r\n\r\n%s",
            ok ? "200 OK" : "503 Service Unavailable", strlen(body), body);
        write(cfd, res, (size_t)res_len);
    }
    else if (strncmp(req, "POST /api/bt/disable", 20) == 0) {
        bt_host_cleanup();
        const char *body = "{\"status\":\"bt_disabled\"}";
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\nContent-Length: %zu\r\n"
            "Connection: close\r\n\r\n%s", strlen(body), body);
        write(cfd, res, (size_t)res_len);
    }
    else if (strncmp(req, "POST /api/bt/forget", 19) == 0) {
        unsigned char mac[6];
        if (!parse_mac_param(req, mac)) send_json(cfd, 400, "{\"error\":\"bad_mac\"}");
        else { bt_host_forget(mac); send_json(cfd, 200, "{\"status\":\"forgotten\"}"); }
    }
    /* ---- Remembered pads + reconnect ---- */
    else if (strncmp(req, "GET /api/bt/paired", 18) == 0) {
        bt_paired_t rows[16];
        int n = bt_host_paired(rows, 16);
        char body[3072]; int blen = 0;
        blen += snprintf(body + blen, sizeof body - (size_t)blen, "{\"radio\":%s,\"pads\":[", bt_host_is_up() ? "true" : "false");
        for (int i = 0; i < n && blen < (int)sizeof body - 200; i++) {
            blen += snprintf(body + blen, sizeof body - (size_t)blen,
                "%s{\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"vid\":%u,\"pid\":%u,\"le\":%d,\"connected\":%d,\"name\":\"%s\"}",
                i ? "," : "", rows[i].mac[5], rows[i].mac[4], rows[i].mac[3], rows[i].mac[2], rows[i].mac[1], rows[i].mac[0],
                rows[i].vid, rows[i].pid, rows[i].le, rows[i].connected, rows[i].name ? rows[i].name : "");
        }
        snprintf(body + blen, sizeof body - (size_t)blen, "]}");
        send_json(cfd, 200, body);
    }
    else if (strncmp(req, "POST /api/bt/reconnect/cancel", 29) == 0) {
        bt_host_reconnect_cancel();
        send_json(cfd, 200, "{\"status\":\"cancelled\"}");
    }
    else if (strncmp(req, "POST /api/bt/reconnect", 22) == 0) {
        unsigned char mac[6];
        if (!parse_mac_param(req, mac)) { send_json(cfd, 400, "{\"error\":\"bad_mac\"}"); }
        else {
            int r = bt_host_reconnect(mac);
            send_json(cfd, r == 1 ? 200 : r == 0 ? 404 : 500,
                      r == 1 ? "{\"status\":\"started\"}" : r == 0 ? "{\"error\":\"not_paired\"}" : "{\"error\":\"bt_init_failed\"}");
        }
    }
    else if (strncmp(req, "GET /api/bt/reconnect", 21) == 0) {
        char body[256];
        bt_host_reconnect_json(body, sizeof body);
        send_json(cfd, 200, body);
    }
    /* ---- Mapping (per-controller button remap) endpoints ---- */
    else if (strncmp(req, "GET /api/mapping/", 17) == 0) {
        char path[256]; char id[24] = {0};
        /* Pull VVVV_PPPP token after /api/mapping/ */
        const char *p = req + 17;
        size_t n_id = 0;
        while (n_id + 1 < sizeof id && (isalnum((unsigned char)*p) || *p == '_')) id[n_id++] = *p++;
        snprintf(path, sizeof path, BT_MAP_DIR "/%s.map", id);
        FILE *f = fopen(path, "r");
        if (f) {
            /* Convert the .map file lines (key = value) into a JSON object. */
            char body[1024]; int blen = 0;
            blen += snprintf(body + blen, sizeof body - blen, "{");
            char line[128]; int first = 1;
            while (fgets(line, sizeof line, f) && blen < (int)sizeof body - 80) {
                char *hash = strchr(line, '#'); if (hash) *hash = 0;
                char *eq = strchr(line, '='); if (!eq) continue;
                *eq = 0; char *k = line, *v = eq + 1;
                while (*k == ' ' || *k == '\t') k++;
                char *ke = k + strlen(k); while (ke > k && (ke[-1]==' '||ke[-1]=='\t'||ke[-1]=='\n'||ke[-1]=='\r')) *--ke = 0;
                while (*v == ' ' || *v == '\t') v++;
                char *ve = v + strlen(v); while (ve > v && (ve[-1]==' '||ve[-1]=='\t'||ve[-1]=='\n'||ve[-1]=='\r')) *--ve = 0;
                if (!*k) continue;
                blen += snprintf(body + blen, sizeof body - blen,
                                 "%s\"%s\":\"%s\"", first ? "" : ",", k, v);
                first = 0;
            }
            fclose(f);
            blen += snprintf(body + blen, sizeof body - blen, "}");
            res_len = snprintf(res, sizeof(res),
                "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\nContent-Length: %d\r\n"
                "Connection: close\r\n\r\n%s", blen, body);
            write(cfd, res, (size_t)res_len);
        } else {
            const char *body = "{\"error\":\"not_found\"}";
            res_len = snprintf(res, sizeof(res),
                "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\nContent-Length: %zu\r\n"
                "Connection: close\r\n\r\n%s", strlen(body), body);
            write(cfd, res, (size_t)res_len);
        }
    }
    else if (strncmp(req, "POST /api/mapping/", 18) == 0) {
        /* Dashboard POSTs a flat JSON object; we persist it as key=value lines. */
        char id[24] = {0};
        const char *p = req + 18;
        size_t n_id = 0;
        while (n_id + 1 < sizeof id && (isalnum((unsigned char)*p) || *p == '_')) id[n_id++] = *p++;
        char *body_start = strstr(req, "\r\n\r\n");
        if (body_start) body_start += 4; else body_start = (char*)req + n;
        mkdir("/data/omnipad", 0755);
        mkdir("/data/omnipad/maps", 0755);
        char path[256];
        snprintf(path, sizeof path, BT_MAP_DIR "/%s.map", id);
        FILE *f = fopen(path, "w");
        if (!f) {
            const char *body = "{\"error\":\"write_failed\"}";
            res_len = snprintf(res, sizeof(res),
                "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\nContent-Length: %zu\r\n"
                "Connection: close\r\n\r\n%s", strlen(body), body);
            write(cfd, res, (size_t)res_len);
        } else {
            fprintf(f, "# OmniPad: written by the mappings page.\n");
            /* Shallow key:value extraction — assumes a flat object with string
             * values, which is what the mappings page sends. */
            char *cur = body_start;
            while (cur && *cur) {
                char *k = strchr(cur, '"'); if (!k) break;
                char *k_end = strchr(k + 1, '"'); if (!k_end) break;
                char *colon = strchr(k_end, ':'); if (!colon) break;
                char *v = strchr(colon, '"'); if (!v) break;
                char *v_end = strchr(v + 1, '"'); if (!v_end) break;
                *k_end = 0; *v_end = 0;
                fprintf(f, "%s = %s\n", k + 1, v + 1);
                cur = v_end + 1;
            }
            fclose(f);
            /* Connected pads pick the new settings up now, no reconnect. */
            bt_host_reload_configs();
            const char *body = "{\"status\":\"saved\"}";
            res_len = snprintf(res, sizeof(res),
                "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\nContent-Length: %zu\r\n"
                "Connection: close\r\n\r\n%s", strlen(body), body);
            write(cfd, res, (size_t)res_len);
        }
    }
    else if (strncmp(req, "GET /img/", 9) == 0) {
        if (!send_asset(cfd, req)) send_json(cfd, 404, "{\"error\":\"not_found\"}");
    }
    else {
        /* "/" and "/mappings" (kept for old bookmarks; the page jumps to
         * its Mappings section) both get the single dashboard page. */
        send_page(cfd);
    }
    close(cfd);
}

void web_poll(long now)
{
    (void)now;
    if (g_server_fd < 0) return;

    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int cfd = accept(g_server_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (cfd >= 0) {
        handle_client(cfd);
    }
}

void web_cleanup(void)
{
    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;
    }
}
