#include "web.h"
#include "ps5_vpad.h"
#include "shellui_inject.h"
#include "version.h"
#include "log.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

static int g_server_fd = -1;

static const char HTML_INDEX[] =
"<!DOCTYPE html>\n"
"<html lang=\"en\">\n"
"<head>\n"
"  <meta charset=\"UTF-8\">\n"
"  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no, viewport-fit=cover\">\n"
"  <meta http-equiv=\"Cache-Control\" content=\"no-cache, no-store, must-revalidate\">\n"
"  <meta http-equiv=\"Pragma\" content=\"no-cache\">\n"
"  <meta http-equiv=\"Expires\" content=\"0\">\n"
"  <title>OmniPad PS5 v1.0.4 (b5)</title>\n"
"  <style>\n"
"    :root {\n"
"      --bg: #070a12;\n"
"      --card: rgba(18, 25, 41, 0.72);\n"
"      --accent: #3b82f6;\n"
"      --accent-glow: rgba(59, 130, 246, 0.35);\n"
"      --success: #10b981;\n"
"      --warning: #f59e0b;\n"
"      --text: #f8fafc;\n"
"      --text-muted: #94a3b8;\n"
"      --border: rgba(255, 255, 255, 0.07);\n"
"    }\n"
"    * { box-sizing: border-box; margin: 0; padding: 0; -webkit-tap-highlight-color: transparent; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; }\n"
"    body { background: radial-gradient(circle at 50% 0%, #152238 0%, var(--bg) 100%); color: var(--text); min-height: 100vh; padding: clamp(14px, 3.5vw, 28px); }\n"
"    .container { max-width: 980px; margin: 0 auto; }\n"
"    header { display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 12px; padding-bottom: 20px; border-bottom: 1px solid var(--border); margin-bottom: 20px; }\n"
"    .logo-group h1 { font-size: clamp(22px, 5vw, 28px); font-weight: 800; background: linear-gradient(135deg, #60a5fa, #c084fc); -webkit-background-clip: text; -webkit-text-fill-color: transparent; letter-spacing: -0.5px; }\n"
"    .logo-group p { font-size: 13px; color: var(--text-muted); margin-top: 3px; }\n"
"    .badge-fw { background: rgba(59, 130, 246, 0.12); color: #93c5fd; border: 1px solid rgba(59, 130, 246, 0.25); padding: 4px 12px; border-radius: 999px; font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: 0.5px; }\n"
"    .info-banner { background: rgba(16, 185, 129, 0.08); border: 1px solid rgba(16, 185, 129, 0.2); color: #a7f3d0; border-radius: 12px; padding: 12px 16px; margin-bottom: 20px; font-size: 13px; line-height: 1.5; display: flex; align-items: center; gap: 10px; }\n"
"    .actions-bar { display: grid; grid-template-columns: 2fr 1fr 1fr 1fr; gap: 10px; margin-bottom: 24px; }\n"
"    @media (max-width: 600px) { .actions-bar { grid-template-columns: 1fr; } }\n"
"    button { background: var(--card); color: var(--text); border: 1px solid var(--border); padding: 12px 16px; border-radius: 10px; font-size: 14px; font-weight: 600; cursor: pointer; transition: all 0.15s ease; display: inline-flex; align-items: center; justify-content: center; gap: 8px; backdrop-filter: blur(12px); min-height: 46px; }\n"
"    button:active { transform: scale(0.97); }\n"
"    button:hover { border-color: rgba(255, 255, 255, 0.2); background: rgba(255, 255, 255, 0.04); }\n"
"    button.btn-primary { background: #2563eb; color: #fff; border: none; box-shadow: 0 4px 16px var(--accent-glow); }\n"
"    button.btn-primary:hover { background: #1d4ed8; }\n"
"    .slots-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(240px, 1fr)); gap: 16px; }\n"
"    .card { background: var(--card); border: 1px solid var(--border); border-radius: 16px; padding: 18px; backdrop-filter: blur(16px); box-shadow: 0 8px 24px rgba(0,0,0,0.25); display: flex; flex-direction: column; transition: border-color 0.2s; }\n"
"    .card.connected { border-color: rgba(16, 185, 129, 0.3); background: rgba(16, 26, 46, 0.8); }\n"
"    .card-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px; }\n"
"    .slot-label { font-size: 12px; font-weight: 700; text-transform: uppercase; letter-spacing: 0.8px; color: var(--text-muted); }\n"
"    .dot { width: 9px; height: 9px; border-radius: 50%; background: #334155; }\n"
"    .dot.active { background: var(--success); box-shadow: 0 0 10px var(--success); }\n"
"    .dot.pending { background: var(--warning); box-shadow: 0 0 10px var(--warning); }\n"
"    .pad-title { font-size: 16px; font-weight: 700; color: #fff; margin-bottom: 12px; min-height: 22px; word-break: break-word; }\n"
"    .pills { display: flex; flex-wrap: wrap; gap: 6px; margin-bottom: 16px; }\n"
"    .pill { display: inline-flex; align-items: center; gap: 5px; background: rgba(255, 255, 255, 0.05); border: 1px solid var(--border); border-radius: 6px; padding: 3px 8px; font-size: 11px; font-weight: 600; color: #cbd5e1; }\n"
"    .pill.green { background: rgba(16, 185, 129, 0.12); color: #6ee7b7; border-color: rgba(16, 185, 129, 0.25); }\n"
"    .pill.blue { background: rgba(59, 130, 246, 0.12); color: #93c5fd; border-color: rgba(59, 130, 246, 0.25); }\n"
"    .meta-list { font-size: 12px; color: var(--text-muted); line-height: 1.7; margin-bottom: 16px; margin-top: auto; }\n"
"    .meta-list span { color: var(--text); font-weight: 500; }\n"
"    .btn-disconnect { width: 100%; color: #f87171; background: rgba(239, 68, 68, 0.08); border-color: rgba(239, 68, 68, 0.2); font-size: 12px; padding: 8px; min-height: 36px; border-radius: 8px; }\n"
"    .btn-disconnect:hover { background: rgba(239, 68, 68, 0.18); }\n"
"    .empty-state { text-align: center; color: #475569; font-size: 13px; font-weight: 500; padding: 24px 0; }\n"
"    footer { text-align: center; color: #64748b; font-size: 12px; margin-top: 36px; }\n"
"  </style>\n"
"</head>\n"
"<body>\n"
"  <div class=\"container\">\n"
"    <header>\n"
"      <div class=\"logo-group\">\n"
"        <h1>OmniPad PS5 <span style=\"font-size:13px;color:#60a5fa;vertical-align:middle;font-weight:600;\">v1.0.4 (b5)</span></h1>\n"
"        <p>Plug & Play USB &bull; 2.4G Dongles & Cable &bull; DualSense Emulation</p>\n"
"      </div>\n"
"      <div class=\"badge-fw\">v1.0.4-b5 &bull; FW 7.00 - 13.60</div>\n"
"    </header>\n"
"\n"
"    <div class=\"info-banner\">\n"
"      <span style=\"font-size: 18px;\">&#128268;</span>\n"
"      <span><strong>Plug & Play 250Hz:</strong> Supports up to 3 emulated virtual controllers (Players 2-4). Player 1 is reserved for the console's physical DualSense.</span>\n"
"    </div>\n"
"\n"
"    <div class=\"actions-bar\">\n"
"      <button class=\"btn-primary\" onclick=\"pressPSAll()\">\n"
"        <span>&#127918;</span> Assign Profile (PS Button)\n"
"      </button>\n"
"      <button onclick=\"window.open('/api/log', '_blank')\">\n"
"        <span>&#128220;</span> View Logs\n"
"      </button>\n"
"      <button onclick=\"refreshData()\">\n"
"        <span>&#8635;</span> Refresh\n"
"      </button>\n"
"      <button onclick=\"shutdownServer()\" style=\"color:#f87171;\">\n"
"        <span>&#9211;</span> Shutdown\n"
"      </button>\n"
"    </div>\n"
"\n"
"    <div class=\"slots-grid\" id=\"slotsContainer\">\n"
"      <!-- Filled dynamically -->\n"
"    </div>\n"
"\n"
"    <footer>\n"
"      OmniPad PS5 v1.0.4 (Build 5) &bull; Universal Controller Hub &bull; Zero Lag 250Hz\n"
"    </footer>\n"
"  </div>\n"
"\n"
"  <script>\n"
"    async function refreshData() {\n"
"      try {\n"
"        const res = await fetch('/api/status');\n"
"        if (!res.ok) return;\n"
"        const data = await res.json();\n"
"        render(data);\n"
"      } catch (e) {}\n"
"    }\n"
"\n"
"    function render(data) {\n"
"      const container = document.getElementById('slotsContainer');\n"
"      container.innerHTML = '';\n"
"\n"
"      data.slots.forEach((s, idx) => {\n"
"        const card = document.createElement('div');\n"
"        const isLive = s.status === 3;\n"
"        const isPending = s.status === 1 || s.status === 2;\n"
"        card.className = 'card' + (isLive ? ' connected' : '');\n"
"        const dotClass = isLive ? 'active' : (isPending ? 'pending' : '');\n"
"        const vdaHandle = s.handle > 0 ? s.handle : (s.alt_handle > 0 ? s.alt_handle : '--');\n"
"        let battPill = '';\n"
"        if (s.conn === 3) {\n"
"          battPill = '<span class=\"pill blue\">&#9889; USB (Wired)</span>';\n"
"        } else if (s.conn === 4) {\n"
"          battPill = '<span class=\"pill blue\">&#128246; 2.4G Dongle</span>';\n"
"        } else if (s.battery > 0 && s.battery <= 100) {\n"
"          battPill = `<span class=\"pill\">&#128267; ${s.battery}%</span>`;\n"
"        }\n"
"\n"
"        if (isLive) {\n"
"          card.innerHTML = `\n"
"            <div class=\"card-header\">\n"
"              <span class=\"slot-label\">Player ${idx + 2}</span>\n"
"              <span class=\"dot ${dotClass}\"></span>\n"
"            </div>\n"
"            <div class=\"pad-title\">${s.name || 'Virtual DualSense'}</div>\n"
"            <div class=\"pills\">\n"
"              <span class=\"pill green\">&#128268; Connected</span>\n"
"              <span class=\"pill blue\">&#9889; 250Hz</span>\n"
"              ${battPill}\n"
"            </div>\n"
"            <div class=\"meta-list\">\n"
"              Injected: <span>${s.injected || 0} frames</span><br>\n"
"              Handle VDA: <span>${vdaHandle}</span><br>\n"
"              User: <span>${s.user_id ? '0x' + s.user_id.toString(16) : 'Primary'}</span>\n"
"            </div>\n"
"            <div style=\"display:grid;grid-template-columns:1fr 1fr;gap:8px;\">\n"
"              <button onclick=\"rebindSlot(${idx})\" style=\"color:#93c5fd;background:rgba(59,130,246,0.12);border-color:rgba(59,130,246,0.25);font-size:12px;padding:8px;min-height:36px;border-radius:8px;\">&#8635; Switch Profile</button>\n"
"              <button class=\"btn-disconnect\" onclick=\"disconnectSlot(${idx})\">&#10005; Disconnect</button>\n"
"            </div>\n"
"          `;\n"
"        } else {\n"
"          card.innerHTML = `\n"
"            <div class=\"card-header\">\n"
"              <span class=\"slot-label\">Player ${idx + 2}</span>\n"
"              <span class=\"dot ${dotClass}\"></span>\n"
"            </div>\n"
"            <div class=\"pad-title\" style=\"color:#64748b;\">${isPending ? 'Identifying...' : 'Slot Available'}</div>\n"
"            <div class=\"empty-state\">${isPending ? 'Syncing with console...' : 'Waiting for USB Controller or 2.4G Dongle'}</div>\n"
"          `;\n"
"        }\n"
"        container.appendChild(card);\n"
"      });\n"
"    }\n"
"\n"
"    async function pressPSAll() {\n"
"      await fetch('/api/press_ps?slot=-1', { method: 'POST' });\n"
"      refreshData();\n"
"    }\n"
"\n"
"    async function rebindSlot(slot) {\n"
"      await fetch('/api/rebind?slot=' + slot, { method: 'POST' });\n"
"      refreshData();\n"
"    }\n"
"\n"
"    async function disconnectSlot(slot) {\n"
"      await fetch('/api/disconnect?slot=' + slot, { method: 'POST' });\n"
"      refreshData();\n"
"    }\n"
"\n"
"    async function shutdownServer() {\n"
"      if (confirm('Are you sure you want to stop OmniPad on PS5?')) {\n"
"        try { await fetch('/api/exit', { method: 'POST' }); } catch(e){}\n"
"        document.body.innerHTML = '<div style=\"text-align:center;padding:50px;color:#94a3b8;\"><h2>OmniPad Stopped</h2><p style=\"margin-top:10px;\">Process successfully terminated on the PS5.</p></div>';\n"
"      }\n"
"    }\n"
"\n"
"    setInterval(refreshData, 1000);\n"
"    refreshData();\n"
"  </script>\n"
"</body>\n"
"</html>\n";

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

    char req[2048];
    ssize_t n = read(cfd, req, sizeof(req) - 1);
    if (n <= 0) { close(cfd); return; }
    req[n] = '\0';

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
            jlen += snprintf(json + jlen, sizeof(json) - (size_t)jlen,
                "%s{\"status\":%d,\"name\":\"%s\",\"conn\":%d,\"handle\":%d,\"alt_handle\":%d,\"user_id\":%u,\"battery\":%d,\"charging\":%d,\"injected\":%u}",
                i > 0 ? "," : "",
                info.status, info.name, info.conn_type, info.handle, info.alt_handle,
                (unsigned)info.user_id, info.battery_level, info.battery_charging,
                (unsigned)info.packets_injected);
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
        vpad_remove(slot);
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
    else {
        /* Serve Web Dashboard HTML */
        size_t hlen = sizeof(HTML_INDEX) - 1;
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=UTF-8\r\n"
            "Cache-Control: no-cache, no-store, must-revalidate\r\n"
            "Pragma: no-cache\r\n"
            "Expires: 0\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n\r\n", hlen);
        write(cfd, res, (size_t)res_len);
        write(cfd, HTML_INDEX, hlen);
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
