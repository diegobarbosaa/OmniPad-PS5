#include "web.h"
#include "http_core.h"
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
#include <stdint.h>
#include <sys/socket.h>
#include <sys/stat.h>
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
"  <title>OmniPad PS5 Universal</title>\n"
"  <style>\n"
"    :root {\n"
"      --bg: #070a12;\n"
"      --card: rgba(18, 25, 41, 0.75);\n"
"      --accent: #3b82f6;\n"
"      --accent-glow: rgba(59, 130, 246, 0.35);\n"
"      --success: #10b981;\n"
"      --warning: #f59e0b;\n"
"      --danger: #ef4444;\n"
"      --text: #f8fafc;\n"
"      --text-muted: #94a3b8;\n"
"      --border: rgba(255, 255, 255, 0.08);\n"
"    }\n"
"    * { box-sizing: border-box; margin: 0; padding: 0; -webkit-tap-highlight-color: transparent; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; }\n"
"    body { background: radial-gradient(circle at 50% 0%, #152238 0%, var(--bg) 100%); color: var(--text); min-height: 100vh; padding: clamp(14px, 3.5vw, 28px); }\n"
"    .container { max-width: 1050px; margin: 0 auto; }\n"
"    header { display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 12px; padding-bottom: 20px; border-bottom: 1px solid var(--border); margin-bottom: 24px; }\n"
"    .logo-group h1 { font-size: clamp(22px, 5vw, 28px); font-weight: 800; background: linear-gradient(135deg, #60a5fa, #c084fc); -webkit-background-clip: text; -webkit-text-fill-color: transparent; letter-spacing: -0.5px; }\n"
"    .logo-group p { font-size: 13px; color: var(--text-muted); margin-top: 3px; }\n"
"    .badge-fw { background: rgba(59, 130, 246, 0.12); color: #93c5fd; border: 1px solid rgba(59, 130, 246, 0.25); padding: 4px 12px; border-radius: 999px; font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: 0.5px; }\n"
"    .actions-bar { display: flex; gap: 10px; margin-bottom: 24px; flex-wrap: wrap; }\n"
"    button { background: var(--card); color: var(--text); border: 1px solid var(--border); padding: 10px 18px; border-radius: 10px; font-size: 13px; font-weight: 600; cursor: pointer; transition: all 0.15s ease; display: inline-flex; align-items: center; justify-content: center; gap: 8px; backdrop-filter: blur(12px); min-height: 42px; }\n"
"    button:active { transform: scale(0.97); }\n"
"    button:hover { border-color: rgba(255, 255, 255, 0.25); background: rgba(255, 255, 255, 0.06); }\n"
"    button.btn-danger { color: #f87171; border-color: rgba(239, 68, 68, 0.3); background: rgba(239, 68, 68, 0.1); }\n"
"    button.btn-danger:hover { background: rgba(239, 68, 68, 0.2); }\n"
"    .token-box { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; margin-bottom: 20px; color: var(--text-muted); font-size: 13px; }\n"
"    .token-box input { background: #0f172a; color: var(--text); border: 1px solid var(--border); border-radius: 8px; padding: 9px 10px; min-width: 220px; }\n"
"    .slots-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(240px, 1fr)); gap: 16px; margin-bottom: 28px; }\n"
"    .card { background: var(--card); border: 1px solid var(--border); border-radius: 16px; padding: 20px; backdrop-filter: blur(16px); box-shadow: 0 8px 24px rgba(0,0,0,0.25); display: flex; flex-direction: column; transition: border-color 0.2s, transform 0.2s; }\n"
"    .card:hover { transform: translateY(-2px); }\n"
"    .card.connected { border-color: rgba(16, 185, 129, 0.35); background: rgba(16, 26, 46, 0.85); }\n"
"    .card-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px; }\n"
"    .slot-label { font-size: 13px; font-weight: 700; text-transform: uppercase; letter-spacing: 0.8px; color: var(--text-muted); }\n"
"    .dot { width: 10px; height: 10px; border-radius: 50%; background: #334155; display: inline-block; }\n"
"    .dot.active { background: var(--success); box-shadow: 0 0 12px var(--success); }\n"
"    .dot.pending { background: var(--warning); box-shadow: 0 0 12px var(--warning); }\n"
"    .pad-title { font-size: 16px; font-weight: 700; color: #fff; margin-bottom: 10px; min-height: 22px; word-break: break-word; }\n"
"    .pills { display: flex; flex-wrap: wrap; gap: 6px; margin-bottom: 14px; }\n"
"    .pill { display: inline-flex; align-items: center; gap: 4px; background: rgba(255, 255, 255, 0.05); border: 1px solid var(--border); border-radius: 6px; padding: 3px 8px; font-size: 11px; font-weight: 600; color: #cbd5e1; }\n"
"    .pill.green { background: rgba(16, 185, 129, 0.12); color: #6ee7b7; border-color: rgba(16, 185, 129, 0.3); }\n"
"    .pill.blue { background: rgba(59, 130, 246, 0.12); color: #93c5fd; border-color: rgba(59, 130, 246, 0.3); }\n"
"    .meta-list { font-size: 13px; color: var(--text-muted); line-height: 1.8; margin-top: auto; }\n"
"    .meta-list span { color: var(--text); font-weight: 600; }\n"
"    .empty-state { text-align: center; color: #64748b; font-size: 13px; font-weight: 500; padding: 24px 0; }\n"
"    footer { text-align: center; color: #64748b; font-size: 12px; margin-top: 36px; line-height: 1.6; }\n"
"  </style>\n"
"</head>\n"
"<body>\n"
"  <div class=\"container\">\n"
"    <header>\n"
"      <div class=\"logo-group\">\n"
"        <h1>OmniPad PS5 Universal</h1>\n"
"        <p>Universal Controller Hub &bull; DualSense Emulation &bull; Zero Lag 250Hz</p>\n"
"      </div>\n"
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
"      <div class=\"badge-fw\">v" ANYPAD_VERSION "-b" STR(ANYPAD_BUILD) " &bull; FW 7.00 - 13.60</div>\n"
"    </header>\n"
"\n"
"    <div class=\"actions-bar\">\n"
"      <button onclick=\"viewLogs()\">\n"
"        <span>&#128220;</span> View Logs\n"
"      </button>\n"
"      <button onclick=\"refreshData()\">\n"
"        <span>&#8635;</span> Refresh\n"
"      </button>\n"
"      <button class=\"btn-danger\" onclick=\"disconnectAll()\">\n"
"        <span>&#128268;</span> Disconnect Controllers\n"
"      </button>\n"
"    </div>\n"
"    <div class=\"token-box\">\n"
"      <label for=\"apiToken\">LAN access token (only when LAN mode is enabled)</label>\n"
"      <input id=\"apiToken\" type=\"password\" autocomplete=\"off\" maxlength=\"128\">\n"
"      <button onclick=\"saveToken()\">Use token</button>\n"
"      <button onclick=\"forgetToken()\">Clear token</button>\n"
"    </div>\n"
"\n"
"    <div class=\"slots-grid\" id=\"slotsContainer\">\n"
"      <!-- Filled dynamically -->\n"
"    </div>\n"
"\n"
"    <footer>\n"
"      OmniPad PS5 Universal v" ANYPAD_VERSION " (Build " STR(ANYPAD_BUILD) ") &bull; Zero Lag 250Hz &bull; Universal Gamepad Engine\n"
"    </footer>\n"
"  </div>\n"
"\n"
"  <script>\n"
"    const tokenKey = 'omnipad-api-token';\n"
"    const tokenInput = document.getElementById('apiToken');\n"
"    tokenInput.value = sessionStorage.getItem(tokenKey) || '';\n"
"    function apiFetch(url, options = {}) {\n"
"      const headers = Object.assign({}, options.headers || {});\n"
"      const token = sessionStorage.getItem(tokenKey);\n"
"      if (token) headers.Authorization = 'Bearer ' + token;\n"
"      return fetch(url, Object.assign({}, options, { headers }));\n"
"    }\n"
"    function saveToken() {\n"
"      const token = tokenInput.value.trim();\n"
"      if (token.length > 128) { alert('Token is too long.'); return; }\n"
"      if (token) sessionStorage.setItem(tokenKey, token);\n"
"      else sessionStorage.removeItem(tokenKey);\n"
"      refreshData();\n"
"    }\n"
"    function forgetToken() { sessionStorage.removeItem(tokenKey); tokenInput.value = ''; }\n"
"    async function viewLogs() {\n"
"      try { const res = await apiFetch('/api/log'); if (!res.ok) { alert('Log request failed: ' + res.status); return; }\n"
"        const url = URL.createObjectURL(await res.blob()); window.open(url, '_blank'); setTimeout(() => URL.revokeObjectURL(url), 60000);\n"
"      } catch (e) { alert('Unable to load logs.'); }\n"
"    }\n"
"    async function refreshData() {\n"
"      try {\n"
"        const res = await apiFetch('/api/status');\n"
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
"        const vdaHandle = s.handle >= 0 ? s.handle : (s.alt_handle > 0 ? s.alt_handle : '--');\n"
"        \n"
"        let connBadge = '';\n"
"        if (s.conn === 4) {\n"
"          connBadge = '<span class=\"pill blue\">&#128225; 2.4G Wireless Dongle</span>';\n"
"        } else if (s.conn === 3) {\n"
"          connBadge = '<span class=\"pill green\">&#128268; USB-C Wired</span>';\n"
"        } else if (s.conn === 1 || s.conn === 2) {\n"
"          connBadge = '<span class=\"pill blue\">&#128246; Bluetooth Direct</span>';\n"
"        } else if (isLive) {\n"
"          connBadge = '<span class=\"pill blue\">&#127918; Connected</span>';\n"
"        }\n"
"\n"
"        const header = document.createElement('div'); header.className = 'card-header';\n"
"        const label = document.createElement('span'); label.className = 'slot-label'; label.textContent = 'Player ' + (idx + 1);\n"
"        const dot = document.createElement('span'); dot.className = 'dot ' + dotClass;\n"
"        header.append(label, dot); card.appendChild(header);\n"
"        const title = document.createElement('div'); title.className = 'pad-title';\n"
"        title.textContent = isLive ? (s.name || 'Virtual DualSense') : (isPending ? 'Identifying...' : 'Slot Available');\n"
"        if (!isLive) title.style.color = '#64748b'; card.appendChild(title);\n"
"        if (isLive) {\n"
"          const pills = document.createElement('div'); pills.className = 'pills';\n"
"          if (connBadge) { const badge = document.createElement('span'); badge.className = 'pill ' + ((s.conn === 3) ? 'green' : 'blue');\n"
"            badge.textContent = s.conn === 4 ? '2.4G Wireless Dongle' : (s.conn === 3 ? 'USB-C Wired' : ((s.conn === 1 || s.conn === 2) ? 'Bluetooth Direct' : 'Connected')); pills.appendChild(badge); }\n"
"          const rate = document.createElement('span'); rate.className = 'pill blue'; rate.textContent = '250Hz Zero-Lag'; pills.appendChild(rate); card.appendChild(pills);\n"
"          const meta = document.createElement('div'); meta.className = 'meta-list';\n"
"          function addMeta(name, value) { const text = document.createTextNode(name + ': '); const span = document.createElement('span'); span.textContent = String(value); meta.append(text, span, document.createElement('br')); }\n"
"          addMeta('Status', 'Connected & Ready'); addMeta('Handle VDA', vdaHandle);\n"
"          addMeta('User', s.user_id ? '0x' + s.user_id.toString(16) : 'Primary'); addMeta('Injected', (s.injected || 0) + ' frames'); card.appendChild(meta);\n"
"        } else {\n"
"          const empty = document.createElement('div'); empty.className = 'empty-state';\n"
"          empty.textContent = isPending ? 'Syncing with console...' : 'Waiting for Controller or 2.4G Dongle'; card.appendChild(empty);\n"
"        }\n"
"        container.appendChild(card);\n"
"      });\n"
"    }\n"
"\n"
"    async function disconnectAll() {\n"
"      try { const res = await apiFetch('/api/disconnect?slot=-1', { method: 'POST' });\n"
"        if (!res.ok) { alert('Disconnect request failed: ' + res.status); return; } refreshData();\n"
"      } catch (e) { alert('Unable to reach the dashboard.'); }\n"
"    }\n"
"\n"
"    setInterval(refreshData, 1000);\n"
"    refreshData();\n"
"  </script>\n"
"</body>\n"
"</html>\n";


#ifndef WEB_TOKEN_PATH
#define WEB_TOKEN_PATH "/data/anypad/web.token"
#endif
#ifndef WEB_STOP_FLAG
#define WEB_STOP_FLAG  "/data/anypad/stop"
#endif
#define WEB_REQUEST_MAX 2048u
#define WEB_RESPONSE_HEADER_MAX 512u
#define WEB_JSON_MAX 2048u

static int g_lan_auth_enabled;
static char g_lan_token[HTTP_AUTH_MAX + 1u];

typedef enum {
    WEB_ROUTE_UNKNOWN = 0,
    WEB_ROUTE_DASHBOARD,
    WEB_ROUTE_STATUS,
    WEB_ROUTE_LOG,
    WEB_ROUTE_PAIR,
    WEB_ROUTE_PRESS_PS,
    WEB_ROUTE_DISCONNECT,
    WEB_ROUTE_REBIND,
    WEB_ROUTE_EXIT,
    WEB_ROUTE_SEND_BUTTON
} web_route_t;

static void clear_token(void)
{
    volatile char *p = g_lan_token;
    for (size_t i = 0; i < sizeof(g_lan_token); i++) p[i] = '\0';
    g_lan_auth_enabled = 0;
}

static int valid_token_char(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
}

static int load_lan_token(char *token, size_t capacity)
{
    int flags = O_RDONLY;
    int fd;
    struct stat st;
    char buffer[HTTP_AUTH_MAX + 3u];
    ssize_t n;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    fd = open(WEB_TOKEN_PATH, flags);
    if (fd < 0) return 0;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
        (st.st_mode & 07777u) != 0600u) {
        close(fd);
        return 0;
    }
    n = read(fd, buffer, sizeof(buffer));
    close(fd);
    if (n <= 0 || (size_t)n >= sizeof(buffer)) return 0;
    buffer[n] = '\0';
    if (n > 0 && buffer[n - 1] == '\n') buffer[--n] = '\0';
    if (n > 0 && buffer[n - 1] == '\r') buffer[--n] = '\0';
    if (n < 32 || n > (ssize_t)HTTP_AUTH_MAX || (size_t)n + 1u > capacity) return 0;
    for (ssize_t i = 0; i < n; i++) {
        if (!valid_token_char((unsigned char)buffer[i])) return 0;
    }
    memcpy(token, buffer, (size_t)n + 1u);
    return 1;
}

static const char *web_dashboard_html(size_t *length)
{
    if (length) *length = sizeof(HTML_INDEX) - 1u;
    return HTML_INDEX;
}

int web_init(int port)
{
    struct sockaddr_in sin;
    int flags;
    int opt = 1;

    web_cleanup();
    if (port < 0 || port > 65535) return 0;
    if (load_lan_token(g_lan_token, sizeof(g_lan_token))) {
        g_lan_auth_enabled = 1;
    } else {
        clear_token();
    }

    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) {
        clear_token();
        return 0;
    }
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((uint16_t)port);
    sin.sin_addr.s_addr = htonl(g_lan_auth_enabled ? INADDR_ANY : INADDR_LOOPBACK);

    if (bind(g_server_fd, (struct sockaddr *)&sin, sizeof(sin)) != 0 ||
        listen(g_server_fd, 8) != 0) {
        close(g_server_fd);
        g_server_fd = -1;
        clear_token();
        return 0;
    }
    flags = fcntl(g_server_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(g_server_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(g_server_fd);
        g_server_fd = -1;
        clear_token();
        return 0;
    }
    if (g_lan_auth_enabled) {
        log_line("web: authenticated LAN dashboard enabled on port %d", port);
    } else {
        log_line("web: local-only dashboard enabled at http://127.0.0.1:%d/", port);
    }
    return 1;
}

static const char *status_reason(int status)
{
    switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    default: return "Error";
    }
}

static int send_response(int fd, int status, const char *content_type,
                        const void *body, size_t body_len, int no_store)
{
    char header[WEB_RESPONSE_HEADER_MAX];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "X-Frame-Options: DENY\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "%s"
        "Content-Length: %lu\r\n"
        "Connection: close\r\n\r\n",
        status, status_reason(status), content_type,
        no_store ? "Cache-Control: no-store\r\n" : "",
        (unsigned long)body_len);
    if (header_len < 0 || (size_t)header_len >= sizeof(header)) return 0;
    if (!http_send_all(fd, header, (size_t)header_len)) return 0;
    return body_len == 0 || http_send_all(fd, body, body_len);
}

static void send_json_error(int fd, int status, const char *message)
{
    char body[160];
    http_builder_t builder;
    http_builder_init(&builder, body, sizeof(body));
    http_builder_append(&builder, "{\"error\":");
    http_builder_append_json_string(&builder, message);
    http_builder_append(&builder, "}");
    if (builder.failed) return;
    send_response(fd, status, "application/json; charset=utf-8",
                  body, builder.length, 1);
}

static web_route_t route_for_path(const char *path)
{
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) return WEB_ROUTE_DASHBOARD;
    if (strcmp(path, "/api/status") == 0) return WEB_ROUTE_STATUS;
    if (strcmp(path, "/api/log") == 0) return WEB_ROUTE_LOG;
    if (strcmp(path, "/api/pair") == 0) return WEB_ROUTE_PAIR;
    if (strcmp(path, "/api/press_ps") == 0) return WEB_ROUTE_PRESS_PS;
    if (strcmp(path, "/api/disconnect") == 0) return WEB_ROUTE_DISCONNECT;
    if (strcmp(path, "/api/rebind") == 0) return WEB_ROUTE_REBIND;
    if (strcmp(path, "/api/exit") == 0) return WEB_ROUTE_EXIT;
    if (strcmp(path, "/api/send_btn") == 0) return WEB_ROUTE_SEND_BUTTON;
    return WEB_ROUTE_UNKNOWN;
}

static int route_method_allowed(web_route_t route, const char *method)
{
    if (route == WEB_ROUTE_DASHBOARD || route == WEB_ROUTE_STATUS ||
        route == WEB_ROUTE_LOG) return strcmp(method, "GET") == 0;
    if (route >= WEB_ROUTE_PAIR && route <= WEB_ROUTE_SEND_BUTTON) {
        return strcmp(method, "POST") == 0;
    }
    return 0;
}

static int route_requires_auth(web_route_t route)
{
    return route != WEB_ROUTE_UNKNOWN &&
           route != WEB_ROUTE_DASHBOARD &&
           route != WEB_ROUTE_STATUS;
}

static int request_authorized(const http_request_t *request)
{
    return request->has_authorization &&
           http_constant_time_equal(request->authorization, g_lan_token);
}

static int is_loopback_host_alias(const char *host)
{
    const char *colon;
    size_t name_length;
    if (!host) return 0;
    colon = strchr(host, ':');
    name_length = colon ? (size_t)(colon - host) : strlen(host);
    if (!((name_length == 9u && strncasecmp(host, "localhost", 9u) == 0) ||
          (name_length == 9u && strncasecmp(host, "127.0.0.1", 9u) == 0))) {
        return 0;
    }
    if (!colon) return 1;
    if (colon[1] == '\0' || strchr(colon + 1, ':')) return 0;
    for (const char *p = colon + 1; *p; p++) {
        if (*p < '0' || *p > '9') return 0;
    }
    return 1;
}

static int request_origin_matches_host(const http_request_t *request)
{
    char expected[HTTP_ORIGIN_MAX + 1u];
    int length;
    if (!request->has_origin) return 1;
    if (!request->has_host || !is_loopback_host_alias(request->host)) return 0;
    length = snprintf(expected, sizeof(expected), "http://%s", request->host);
    return length >= 0 && (size_t)length < sizeof(expected) &&
           strcasecmp(request->origin, expected) == 0;
}

static void send_json_literal(int fd, int status, const char *json)
{
    send_response(fd, status, "application/json; charset=utf-8",
                  json, strlen(json), 1);
}

static int request_read(int fd, char *buffer, size_t capacity, size_t *length)
{
    size_t used = 0;
    long deadline = now_ms() + 100L;
    struct pollfd pfd;

    pfd.fd = fd;
    pfd.events = POLLIN;
    while (used < capacity) {
        long remaining = deadline - now_ms();
        int ready;
        if (remaining <= 0) return 408;
        ready = poll(&pfd, 1, remaining > 100L ? 100 : (int)remaining);
        if (ready < 0) {
            if (errno == EINTR) continue;
            return 400;
        }
        if (ready == 0) return 408;
        if ((pfd.revents & (POLLERR | POLLNVAL)) != 0) return 400;

        ssize_t received = recv(fd, buffer + used, capacity - used, 0);
        if (received == 0) return 400;
        if (received < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return 400;
        }
        used += (size_t)received;
        size_t header_len = http_header_length(buffer, used);
        if (header_len != 0) {
            if (header_len != used) return 400;
            *length = used;
            return 200;
        }
    }
    return 431;
}

static int query_value(const http_request_t *request, const char *key,
                       char *value, size_t capacity)
{
    int found = http_query_get(request, key, value, capacity);
    return found == 1;
}

static int query_slot(const http_request_t *request, int allow_all, int *slot)
{
    char value[24];
    long parsed;
    if (!query_value(request, "slot", value, sizeof(value)) ||
        !http_parse_long(value, 10, allow_all ? -1 : 0,
                         MAX_SLOTS - 1, &parsed)) return 0;
    *slot = (int)parsed;
    return 1;
}

static int send_status(int fd)
{
    char json[WEB_JSON_MAX];
    http_builder_t builder;
    http_builder_init(&builder, json, sizeof(json));
    if (!http_builder_appendf(&builder,
        "{\"version\":\"%s\",\"build\":%d,\"slots\":[",
        ANYPAD_VERSION, ANYPAD_BUILD)) return 0;

    for (int i = 0; i < MAX_SLOTS; i++) {
        vpad_slot_info_t info;
        memset(&info, 0, sizeof(info));
        vpad_get_slot_info(i, &info);
        if (!http_builder_appendf(&builder,
            "%s{\"status\":%d,\"name\":", i > 0 ? "," : "", info.status) ||
            !http_builder_append_json_string(&builder, info.name) ||
            !http_builder_appendf(&builder,
            ",\"conn\":%d,\"handle\":%d,\"alt_handle\":%d,\"user_id\":%u,"
            "\"battery\":%d,\"charging\":%d,\"injected\":%u}",
            info.conn_type, info.handle, info.alt_handle, (unsigned)info.user_id,
            info.battery_level, info.battery_charging,
            (unsigned)info.packets_injected)) {
            return 0;
        }
    }
    if (!http_builder_append(&builder, "]}")) return 0;
    return send_response(fd, 200, "application/json; charset=utf-8",
                         json, builder.length, 1);
}

static int button_mask_from_name(const char *name, uint32_t *mask)
{
    static const struct {
        const char *name;
        uint32_t mask;
    } buttons[] = {
        {"dpad_up", PAD_DPAD_UP}, {"dpad_down", PAD_DPAD_DOWN},
        {"dpad_left", PAD_DPAD_LEFT}, {"dpad_right", PAD_DPAD_RIGHT},
        {"circle", PAD_BTN_CIRCLE}, {"cross", PAD_BTN_CROSS},
        {"square", PAD_BTN_SQUARE}, {"triangle", PAD_BTN_TRIANGLE},
        {"options", PAD_BTN_OPTIONS}, {"share", PAD_BTN_SHARE},
        {"ps", PAD_BTN_PS}
    };
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        if (strcmp(name, buttons[i].name) == 0) {
            *mask = buttons[i].mask;
            return 1;
        }
    }
    return 0;
}

static int button_mask_from_code(long code, uint32_t *mask)
{
    static const uint32_t masks[] = {
        0, PAD_BTN_CROSS, PAD_BTN_CIRCLE, PAD_DPAD_UP, PAD_DPAD_DOWN,
        PAD_DPAD_LEFT, PAD_DPAD_RIGHT, PAD_BTN_PS, PAD_BTN_SQUARE,
        PAD_BTN_TRIANGLE, PAD_BTN_OPTIONS, PAD_BTN_SHARE, PAD_BTN_L1, PAD_BTN_R1
    };
    if (code < 1 || code > 13) return 0;
    *mask = masks[code];
    return 1;
}

static int handle_api(int fd, const http_request_t *request, web_route_t route)
{
    if (route == WEB_ROUTE_STATUS) {
        if (!send_status(fd)) send_json_error(fd, 500, "response too large");
        return 1;
    }
    if (route == WEB_ROUTE_DASHBOARD) {
        size_t html_len;
        const char *html = web_dashboard_html(&html_len);
        send_response(fd, 200, "text/html; charset=utf-8",
                      html, html_len, 1);
        return 1;
    }
    if (route == WEB_ROUTE_LOG) {
        int log_fd = open("/data/anypad/anypad.log", O_RDONLY);
        if (log_fd < 0) log_fd = open("anypad.log", O_RDONLY);
        if (log_fd < 0) {
            send_json_error(fd, 404, "log file not found");
            return 1;
        }
        off_t size = lseek(log_fd, 0, SEEK_END);
        if (size < 0) size = 0;
        off_t start = size > 32768 ? size - 32768 : 0;
        if (lseek(log_fd, start, SEEK_SET) < 0) {
            close(log_fd);
            send_json_error(fd, 500, "unable to read log");
            return 1;
        }
        char log_buffer[32768];
        ssize_t read_len = read(log_fd, log_buffer, sizeof(log_buffer));
        close(log_fd);
        if (read_len < 0) {
            send_json_error(fd, 500, "unable to read log");
            return 1;
        }
        send_response(fd, 200, "text/plain; charset=utf-8",
                      log_buffer, (size_t)read_len, 1);
        return 1;
    }

    if (route == WEB_ROUTE_PAIR) {
        send_json_literal(fd, 200, "{\"status\":\"usb_plug_and_play_active\"}");
        return 1;
    }

    int slot;
    if (route == WEB_ROUTE_PRESS_PS) {
        if (!query_slot(request, 1, &slot)) {
            send_json_error(fd, 400, "valid slot parameter required");
            return 1;
        }
        if (slot < 0) {
            for (int i = 0; i < MAX_SLOTS; i++) {
                if (vpad_is_live(i)) {
                    vpad_slot_info_t info;
                    memset(&info, 0, sizeof(info));
                    vpad_get_slot_info(i, &info);
                    shellui_press_ps_button(info.handle);
                    vpad_press_ps_button(i);
                }
            }
        } else if (vpad_is_live(slot)) {
            vpad_slot_info_t info;
            memset(&info, 0, sizeof(info));
            vpad_get_slot_info(slot, &info);
            shellui_press_ps_button(info.handle);
            vpad_press_ps_button(slot);
        }
        send_json_literal(fd, 200, "{\"status\":\"ps_pressed\"}");
        return 1;
    }
    if (route == WEB_ROUTE_DISCONNECT) {
        if (!query_slot(request, 1, &slot)) {
            send_json_error(fd, 400, "valid slot parameter required");
            return 1;
        }
        if (slot < 0) {
            for (int i = 0; i < MAX_SLOTS; i++) vpad_remove(i);
        } else {
            vpad_remove(slot);
        }
        send_json_literal(fd, 200, "{\"status\":\"disconnected\"}");
        return 1;
    }
    if (route == WEB_ROUTE_REBIND) {
        char user_value[32];
        long user_id;
        if (!query_slot(request, 0, &slot) ||
            !query_value(request, "user", user_value, sizeof(user_value)) ||
            !http_parse_long(user_value, 0, -1, INT32_MAX, &user_id)) {
            send_json_error(fd, 400, "valid slot and user parameters required");
            return 1;
        }
        if (!vpad_rebind_user(slot, (int32_t)user_id)) {
            send_json_error(fd, 409, "slot is not ready for user assignment");
            return 1;
        }
        send_json_literal(fd, 200, "{\"status\":\"rebound\"}");
        return 1;
    }
    if (route == WEB_ROUTE_EXIT) {
        static const char response[] = "{\"status\":\"shutting_down\"}";
        send_response(fd, 200, "application/json; charset=utf-8",
                      response, sizeof(response) - 1u, 1);
        int stop_fd = open(WEB_STOP_FLAG, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (stop_fd >= 0) close(stop_fd);
        return 1;
    }
    if (route == WEB_ROUTE_SEND_BUTTON) {
        char code_value[16];
        char button_value[32];
        int has_code = http_query_get(request, "code", code_value, sizeof(code_value));
        int has_button = http_query_get(request, "btn", button_value, sizeof(button_value));
        uint32_t button_mask;
        long code;
        if (!query_slot(request, 1, &slot) ||
            has_code < 0 || has_button < 0 ||
            (has_code == 0 && has_button == 0) ||
            (has_code == 1 && has_button == 1) ||
            (has_code == 1 && (!http_parse_long(code_value, 10, 1, 13, &code) ||
                               !button_mask_from_code(code, &button_mask))) ||
            (has_button == 1 && !button_mask_from_name(button_value, &button_mask))) {
            send_json_error(fd, 400, "valid slot and button parameters required");
            return 1;
        }

        if ((button_mask & PAD_BTN_PS) != 0) {
            if (slot < 0) {
                for (int i = 0; i < MAX_SLOTS; i++) {
                    if (vpad_is_live(i)) {
                        vpad_slot_info_t info;
                        memset(&info, 0, sizeof(info));
                        vpad_get_slot_info(i, &info);
                        shellui_press_ps_button(info.handle);
                    }
                }
            } else if (vpad_is_live(slot)) {
                vpad_slot_info_t info;
                memset(&info, 0, sizeof(info));
                vpad_get_slot_info(slot, &info);
                shellui_press_ps_button(info.handle);
            }
        }

        if (slot < 0) {
            for (int i = 0; i < MAX_SLOTS; i++) {
                if (vpad_is_live(i)) {
                    pad_state_t state;
                    pad_state_neutral(&state);
                    state.buttons = button_mask;
                    vpad_update(i, &state);
                }
            }
            usleep(150000);
            for (int i = 0; i < MAX_SLOTS; i++) {
                if (vpad_is_live(i)) {
                    pad_state_t state;
                    pad_state_neutral(&state);
                    vpad_update(i, &state);
                }
            }
        } else if (vpad_is_live(slot)) {
            pad_state_t state;
            pad_state_neutral(&state);
            state.buttons = button_mask;
            vpad_update(slot, &state);
            usleep(150000);
            pad_state_neutral(&state);
            vpad_update(slot, &state);
        }
        send_json_literal(fd, 200, "{\"status\":\"button_sent\"}");
        return 1;
    }

    send_json_error(fd, 404, "not found");
    return 1;
}

static void handle_client(int fd)
{
    struct timeval timeout;
    char raw_request[WEB_REQUEST_MAX];
    size_t request_len = 0;
    http_request_t request;
    web_route_t route;

    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    int read_status = request_read(fd, raw_request, sizeof(raw_request), &request_len);
    if (read_status != 200) {
        send_json_error(fd, read_status, read_status == 431 ?
                        "request headers too large" :
                        (read_status == 408 ? "request timed out" : "malformed request"));
        return;
    }
    if (!http_request_parse(raw_request, request_len, &request)) {
        send_json_error(fd, 400, "malformed request");
        return;
    }
    if (!g_lan_auth_enabled && request.has_host &&
        !is_loopback_host_alias(request.host)) {
        send_json_error(fd, 403, "non-loopback host rejected");
        return;
    }
    if (request.content_length > 0) {
        send_json_error(fd, 413, "request bodies are not supported");
        return;
    }

    route = route_for_path(request.path);
    if (route == WEB_ROUTE_UNKNOWN) {
        send_json_error(fd, 404, "not found");
        return;
    }
    if (!route_method_allowed(route, request.method)) {
        send_json_error(fd, 405, "method not allowed");
        return;
    }
    if (g_lan_auth_enabled && route_requires_auth(route) &&
        !request_authorized(&request)) {
        send_json_error(fd, 401, "authorization required");
        return;
    }
    if (!g_lan_auth_enabled && route_requires_auth(route) &&
        !request_origin_matches_host(&request)) {
        send_json_error(fd, 403, "cross-origin control request rejected");
        return;
    }
    handle_api(fd, &request, route);
}

void web_poll(long now)
{
    (void)now;
    if (g_server_fd < 0) return;

    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = accept(g_server_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (client_fd >= 0) {
        handle_client(client_fd);
        close(client_fd);
    }
}

#ifdef WEB_TESTING
int web_test_bound_port(void)
{
    struct sockaddr_in sin;
    socklen_t length = sizeof(sin);
    if (g_server_fd < 0 || getsockname(g_server_fd, (struct sockaddr *)&sin, &length) != 0) return -1;
    return (int)ntohs(sin.sin_port);
}

int web_test_lan_auth_enabled(void)
{
    return g_lan_auth_enabled;
}
#endif

void web_cleanup(void)
{
    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;
    }
    clear_token();
}
