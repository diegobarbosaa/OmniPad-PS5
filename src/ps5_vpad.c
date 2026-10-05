#include "ps5_vpad.h"
#include "shellui_inject.h"
#include "log.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define VIRTUAL_DEVICE_DUALSENSE 3
#define T_IDENTIFY               3000   /* ms to wait for DEVICE_ADDED */
#define T_AMBIGUITY               300   /* ms after match before committing */
#define LINE_MAX_LEN             1024

#ifdef __PROSPERO__
extern int32_t sceUserServiceInitialize(void *params);
extern int32_t sceUserServiceGetInitialUser(int32_t *user);
extern int32_t sceUserServiceGetForegroundUser(int32_t *user);
extern int32_t sceUserServiceGetLoginUserIdList(int32_t list[4]);
extern int32_t scePadInit(void);
extern int32_t scePadGetHandle(int32_t user_id, int32_t port_type, int32_t index);
extern int32_t scePadSetProcessPrivilege(int32_t privilege);
extern int32_t scePadVirtualDeviceAddDevice(void *param, int32_t deviceType);
extern int32_t scePadVirtualDeviceInsertData(int32_t handle, const void *data);
extern int32_t scePadVirtualDeviceDeleteDevice(int32_t handle);
extern uint64_t sceKernelGetProcessTime(void);
#endif

typedef int32_t (*mbus_bind_fn)(uint64_t device_id, int32_t user);


typedef struct {
    vpad_status_t   status;
    int             remove_when_found;
    int32_t         handle;
    int32_t         alt_handle;
    uint64_t        device_id;
    int32_t         user_id;
    pad_conn_type_t conn_type;
    char            name[64];
    uint8_t         battery_level;
    uint8_t         battery_charging;
    uint32_t        packets_injected;
    long            connected_time;
    long            last_update_time;
    pad_state_t     pad_state;      /* Latch state for continuous 250Hz injection */
} internal_slot_t;

static internal_slot_t g_slots[MAX_SLOTS];
static pthread_mutex_t g_vpad_mutex = PTHREAD_MUTEX_INITIALIZER;
static mbus_bind_fn g_bind = NULL;
static int g_ready = 0;
static int32_t g_active_user = -1;

/* Log reader state */
static int g_pending = -1;
static long g_t_add = 0, g_t_match = 0;
static int g_matches = 0;
static uint64_t g_match_dev = 0;
static int g_klog_handle = -1;
static int g_log_fd = -1;
static int g_log_is_dev = 0;
static char g_line[LINE_MAX_LEN];
static size_t g_line_len = 0;

static inline int plausible_handle(int32_t h)
{
    return h > 0 && h < 64;
}

static int32_t get_user_id_for_slot(int slot)
{
    int32_t user = -1;
#ifdef __PROSPERO__
    int32_t list[4] = {-1, -1, -1, -1};
    if (sceUserServiceGetLoginUserIdList(list) == 0) {
        /* If slot 0 is connecting, check if list[0] is already occupied by native controller */
        if (slot == 0 && list[0] > 0) {
            int h0 = scePadGetHandle(list[0], 0, 0);
            if (h0 > 0 && list[1] > 0) {
                log_line("vpad: user 0x%08x already has active pad handle %d, assigning slot 0 to user 0x%08x",
                         (unsigned)list[0], h0, (unsigned)list[1]);
                return list[1];
            }
        }
        if (slot >= 0 && slot < 4 && list[slot] > 0) {
            return list[slot];
        }
    }
    if (sceUserServiceGetForegroundUser(&user) == 0 && user > 0)
        return user;
    if (sceUserServiceGetInitialUser(&user) == 0 && user > 0)
        return user;
#endif
    return (slot >= 0 && slot < 4) ? (0x10000000 + slot) : 0x10000000;
}

static int scan_vpad_handle(int32_t user_id, int slot)
{
    (void)slot;
#ifdef __PROSPERO__
    int users[4];
    int count = 0;
    int foreground = 0;
    if (user_id > 0) users[count++] = user_id;
    if (sceUserServiceGetForegroundUser(&foreground) == 0 && foreground > 0 && foreground != user_id)
        users[count++] = foreground;
    users[count++] = 1;
    users[count++] = 0x10000000;

    for (int u = 0; u < count; u++) {
        for (int idx = 0; idx < 4; idx++) {
            int h = scePadGetHandle(users[u], VIRTUAL_DEVICE_DUALSENSE, idx);
            if (plausible_handle(h)) return h;
            h = scePadGetHandle(users[u], 0, idx);
            if (plausible_handle(h)) return h;
        }
    }
#else
    (void)user_id;
#endif
    return -1;
}

static int open_kernel_log(void)
{
    /* Try local klog server (port 3232, kstuff-1.13) first */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd >= 0) {
        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_port = htons(3232);
        sin.sin_addr.s_addr = inet_addr("127.0.0.1");
        if (connect(fd, (struct sockaddr *)&sin, sizeof(sin)) == 0) {
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
            g_log_is_dev = 0;
            return fd;
        }
        close(fd);
    }

    /* Fallback directly to FreeBSD /dev/klog */
    fd = open("/dev/klog", O_RDONLY | O_NONBLOCK);
    if (fd >= 0) {
        g_log_is_dev = 1;
        return fd;
    }

    log_line("vpad: unable to open klog server or /dev/klog (errno=%d)", errno);
    return -1;
}

static void close_kernel_log(void)
{
    if (g_log_fd >= 0) {
        close(g_log_fd);
        g_log_fd = -1;
    }
    g_line_len = 0;
}

static const char *find_word(const char *line, const char *word)
{
    size_t wlen = strlen(word);
    const char *p = line;
    while ((p = strstr(p, word)) != NULL) {
        if (p == line || !( (p[-1] >= 'A' && p[-1] <= 'Z') ||
                             (p[-1] >= 'a' && p[-1] <= 'z') ||
                             (p[-1] >= '0' && p[-1] <= '9') ||
                              p[-1] == '_')) {
            return p;
        }
        p += wlen;
    }
    return NULL;
}

static int parse_klog_handle(const char *line)
{
    const char *p = strstr(line, "Open Pad");
    if (!p) p = strstr(line, "open pad");
    if (p) {
        const char *r = strstr(p, "ret=");
        if (r) {
            int h = (int)strtol(r + 4, NULL, 0);
            if (plausible_handle(h)) return h;
        }
    }
    return -1;
}

static uint64_t parse_device_id_from_klog(const char *line)
{
    if (!strstr(line, "DEVICE_ADDED") && !strstr(line, "device_added") && !strstr(line, "DeviceAdded"))
        return 0;

    const char *st = strstr(line, "subType");
    if (!st) st = strstr(line, "subtype");
    if (!st) st = strstr(line, "SubType");
    if (st) {
        st += 7;
        while (*st == ' ' || *st == ':' || *st == '=') st++;
        if (strncmp(st, "22", 2) != 0 && strncmp(st, "0x16", 4) != 0) {
            return 0;
        }
    }

    const char *p = find_word(line, "DeviceId");
    if (!p) p = find_word(line, "deviceId");
    if (!p) p = find_word(line, "dev_id");
    if (!p) return 0;

    p += (p[0] == 'd' && p[1] == 'e' && p[2] == 'v') ? 6 : 8;
    while (*p == ' ' || *p == ':' || *p == '=' ) p++;
    return hex_to_u64(p);
}

static void poll_kernel_log(void)
{
    char buf[512];
    for (int r = 0; r < 8 && g_log_fd >= 0; r++) {
        ssize_t n = read(g_log_fd, buf, sizeof(buf));
        if (n <= 0) break;
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] != '\n' && g_line_len < sizeof(g_line) - 1) {
                g_line[g_line_len++] = buf[i];
                continue;
            }
            g_line[g_line_len] = '\0';
            g_line_len = 0;

            int kh = parse_klog_handle(g_line);
            if (kh > 0 && g_klog_handle <= 0) {
                g_klog_handle = kh;
                log_line("vpad: captured Open Pad handle %d from klog", kh);
            }

            uint64_t dev = parse_device_id_from_klog(g_line);
            if (dev && g_pending >= 0) {
                if (g_matches == 0) {
                    g_match_dev = dev;
                    g_t_match = now_ms();
                }
                if (g_matches == 0 || dev != g_match_dev)
                    g_matches++;
            }
        }
    }
}

int vpad_init(void)
{
    if (g_ready) return 1;

    if (!elevate_privileges()) {
        log_line("vpad: failed to elevate privileges");
        return 0;
    }

#ifdef __PROSPERO__
    sceUserServiceInitialize(NULL);

    void *mbus = dlopen("/system/common/lib/libSceMbus.sprx", RTLD_NOW | RTLD_GLOBAL);
    if (!mbus) mbus = dlopen("libSceMbus.sprx", RTLD_NOW | RTLD_GLOBAL);
    if (mbus) {
        g_bind = (mbus_bind_fn)dlsym(mbus, "sceMbusBindDeviceWithUserId");
    }

    if (!g_bind) {
        log_line("vpad: local libSceMbus unavailable, SceShellCore remote injection will be used");
    }

    int r = scePadInit();
    if (r != 0) {
        log_line("vpad: scePadInit returned 0x%08x", (unsigned)r);
        return 0;
    }

    /* Start with process privilege 1 to manage virtual pads */
    scePadSetProcessPrivilege(1);
#endif

    g_active_user = get_user_id_for_slot(0);
    log_line("vpad: initialized successfully, default user 0x%08x", (unsigned)g_active_user);
    g_ready = 1;
    return 1;
}

int vpad_add(int slot, pad_conn_type_t type, const char *name)
{
    if (!g_ready || slot < 0 || slot >= MAX_SLOTS) return 0;

    pthread_mutex_lock(&g_vpad_mutex);
    if (g_slots[slot].status != VP_FREE && g_slots[slot].status != VP_FAILED) {
        pthread_mutex_unlock(&g_vpad_mutex);
        return 0;
    }

    memset(&g_slots[slot], 0, sizeof(internal_slot_t));
    g_slots[slot].status = VP_QUEUED;
    g_slots[slot].handle = -1;
    g_slots[slot].alt_handle = -1;
    g_slots[slot].conn_type = type;
    if (name) snprintf(g_slots[slot].name, sizeof(g_slots[slot].name), "%s", name);
    g_slots[slot].connected_time = now_ms();
    pad_state_neutral(&g_slots[slot].pad_state);

#ifdef __PROSPERO__
    /* Ensure process privilege is active when a controller connects */
    scePadSetProcessPrivilege(1);
#endif
    pthread_mutex_unlock(&g_vpad_mutex);

    log_line("vpad: slot %d queued (%s via %s)", slot,
             g_slots[slot].name, type == CONN_USB_WIRED ? "USB" : "Bluetooth");
    return 1;
}

static void start_identification(int slot)
{
    struct {
        int32_t size;
        int32_t user_id;
        int32_t pad[6];
    } param;
    const int32_t sentinel = 0x7EADBEEF;

    memset(&param, 0, sizeof(param));
    param.size = (int32_t)sizeof(param);
    param.user_id = 1;
    for (int i = 0; i < 6; i++) param.pad[i] = sentinel;

    g_slots[slot].user_id = get_user_id_for_slot(slot);
    elevate_privileges();

    g_log_fd = open_kernel_log();
    if (g_log_fd >= 0) {
        /* Drain klog backlog before adding device */
        char drain[1024];
        while (read(g_log_fd, drain, sizeof(drain)) > 0);
        g_line_len = 0;
    } else {
        log_line("vpad: slot %d warning: klog unavailable, proceeding directly", slot);
    }

    g_matches = 0;
    g_match_dev = 0;
    g_klog_handle = -1;

    int32_t ret = 0;
#ifdef __PROSPERO__
    ret = scePadVirtualDeviceAddDevice(&param, VIRTUAL_DEVICE_DUALSENSE);
#endif
    log_line("vpad: slot %d AddDevice -> 0x%08x", slot, (unsigned)ret);

    int handle = -1;
    if (plausible_handle(ret)) {
        handle = ret;
    } else {
        /* Check if kernel output the allocated handle into param.pad[i] */
        for (int i = 0; i < 6; i++) {
            if (param.pad[i] != sentinel && plausible_handle(param.pad[i])) {
                handle = param.pad[i];
                log_line("vpad: slot %d handle found in param.pad[%d]: %d", slot, i, handle);
                break;
            }
        }
    }

    /* If local add failed with non-pending error, try SceShellCore remote VDA */
    if (handle <= 0 && (uint32_t)ret != 0x803B0006u) {
        int code = 0;
        int remote_h = shellcore_vda(&code);
        if (plausible_handle(remote_h)) {
            handle = remote_h;
            log_line("vpad: slot %d remote SceShellCore AddDevice returned handle %d", slot, handle);
        }
    }

    g_slots[slot].handle = handle;
    g_slots[slot].alt_handle = -1;

    if (g_log_fd >= 0) {
        g_pending = slot;
        g_t_add = now_ms();
        g_slots[slot].status = VP_PENDING;

        if ((uint32_t)ret == 0x803B0006u) {
            log_line("vpad: slot %d assignment pending (0x803B0006), awaiting device_id to complete MBus bind", slot);
        }
    } else {
        if (handle <= 0) {
            handle = scan_vpad_handle(g_slots[slot].user_id, slot);
            g_slots[slot].handle = handle;
        }
        if (handle > 0) {
            g_slots[slot].status = VP_READY;
            notify_ps5("OmniPad: %s connected (Slot %d)", g_slots[slot].name[0] ? g_slots[slot].name : "Controller", slot + 1);
            shellui_press_ps_button(handle);
        } else {
            g_slots[slot].status = VP_FAILED;
        }
    }
}

static void finish_identification(long now)
{
    internal_slot_t *s = &g_slots[g_pending];
    int slot = g_pending;

    if (g_matches == 0 && now - g_t_add < T_IDENTIFY) return;
    if (g_matches == 1 && now - g_t_match < T_AMBIGUITY && now - g_t_add < T_IDENTIFY) return;

    g_pending = -1;
    close_kernel_log();

    if (g_matches == 1) {
        s->device_id = g_match_dev;
    } else {
        log_line("vpad: slot %d device_id not uniquely resolved in klog (%d matches)", slot, g_matches);
    }

    /* Handle resolution:
     * 1. If s->handle is not plausible, check klog handle.
     * 2. If still not plausible, scan via scePadGetHandle.
     * 3. Set alt_handle if device_id <= 0x7FFFFFFF.
     */
    if (!plausible_handle(s->handle)) {
        if (plausible_handle(g_klog_handle)) {
            s->handle = g_klog_handle;
            log_line("vpad: slot %d using klog handle %d", slot, s->handle);
        } else {
            int sc = scan_vpad_handle(s->user_id, slot);
            if (plausible_handle(sc)) {
                s->handle = sc;
                log_line("vpad: slot %d resolved via scePadGetHandle: %d", slot, s->handle);
            }
        }
    }

    if (s->device_id > 0 && s->device_id <= 0x7FFFFFFF) {
        /* On FW 13.60, the device_id in system log is the exact handle InsertData accepts */
        s->alt_handle = s->handle;
        s->handle = (int32_t)s->device_id;
        log_line("vpad: slot %d using device_id as primary handle: %d (alt: %d)",
                 slot, s->handle, s->alt_handle);
    }

    if (s->remove_when_found) {
#ifdef __PROSPERO__
        if (s->handle > 0) scePadVirtualDeviceDeleteDevice(s->handle);
        if (s->alt_handle > 0 && s->alt_handle != s->handle) scePadVirtualDeviceDeleteDevice(s->alt_handle);
#endif
        memset(s, 0, sizeof(*s));
        return;
    }

    s->status = VP_READY;
    notify_ps5("OmniPad: %s connected (Slot %d)", s->name[0] ? s->name : "Controller", slot + 1);

    /* Bind device to user in SceShellUI (synchronous attempt first) */
    if (s->device_id > 0) {
        int32_t bound = -1;
        if (g_bind) bound = g_bind(s->device_id, s->user_id);
        if (bound != 0) bound = shellui_remote_bind_device(s->device_id, s->user_id);
        log_line("vpad: slot %d MBus bind dev 0x%llx user 0x%08x -> %d",
                 slot, (unsigned long long)s->device_id, (unsigned)s->user_id, (int)bound);
    }

    /* Press PS button inside SceShellUI so user account prompt opens/assigns */
    int resolved = shellui_press_ps_button(s->handle);
    if (resolved > 0 && s->handle <= 0) {
        s->handle = resolved;
        log_line("vpad: slot %d shellui resolved handle %d", slot, s->handle);
    }

    log_line("vpad: slot %d ready (handle %d, alt %d, dev 0x%llx, user 0x%08x)",
             slot, s->handle, s->alt_handle, (unsigned long long)s->device_id, (unsigned)s->user_id);
}

void vpad_poll(long now)
{
    if (!g_ready) return;

    pthread_mutex_lock(&g_vpad_mutex);
    if (g_pending >= 0) {
        poll_kernel_log();
        finish_identification(now);
        pthread_mutex_unlock(&g_vpad_mutex);
    } else {
        /* Check if we need to start an identification */
        for (int i = 0; i < MAX_SLOTS; i++) {
            if (g_slots[i].status == VP_QUEUED) {
                start_identification(i);
                break;
            }
        }
        pthread_mutex_unlock(&g_vpad_mutex);
    }

    /* Continuous 250Hz injection of the last known state for all READY pads */
    for (int i = 0; i < MAX_SLOTS; i++) {
        pthread_mutex_lock(&g_vpad_mutex);
        if (g_slots[i].status == VP_READY && (g_slots[i].handle > 0 || g_slots[i].alt_handle > 0)) {
            if (g_slots[i].handle <= 0 && g_slots[i].alt_handle > 0) {
                g_slots[i].handle = g_slots[i].alt_handle;
                g_slots[i].alt_handle = -1;
            }
            PadData d;
            uint64_t pad_time = 0;
#ifdef __PROSPERO__
            pad_time = sceKernelGetProcessTime();
            if (pad_time > 1000) pad_time -= 1000;
#else
            pad_time = (uint64_t)now * 1000ULL;
#endif
            pad_data_from_state(&d, &g_slots[i].pad_state, pad_time);

            int32_t handle = g_slots[i].handle;
            int32_t alt_handle = g_slots[i].alt_handle;
            g_slots[i].packets_injected++;
            pthread_mutex_unlock(&g_vpad_mutex);

#ifdef __PROSPERO__
            int r = scePadVirtualDeviceInsertData(handle, &d);
            if (r != 0 && alt_handle > 0) {
                int r2 = scePadVirtualDeviceInsertData(alt_handle, &d);
                if (r2 == 0) {
                    /* alt_handle accepted! Switch handles dynamically */
                    pthread_mutex_lock(&g_vpad_mutex);
                    g_slots[i].handle = alt_handle;
                    g_slots[i].alt_handle = handle;
                    pthread_mutex_unlock(&g_vpad_mutex);
                    log_line("vpad: slot %d switched to alt_handle %d (primary %d failed 0x%08x)",
                             i, alt_handle, handle, (unsigned)r);
                    r = 0;
                }
            }
            static long s_last_insert_err[MAX_SLOTS] = {0};
            if (r != 0 && (now - s_last_insert_err[i] > 3000)) {
                s_last_insert_err[i] = now;
                log_line("vpad: slot %d InsertData(h=%d) error 0x%08x", i, handle, (unsigned)r);
            }
#else
            (void)handle; (void)alt_handle;
#endif
        } else {
            pthread_mutex_unlock(&g_vpad_mutex);
        }
    }
}

void vpad_remove(int slot)
{
    if (slot < 0 || slot >= MAX_SLOTS) return;

    pthread_mutex_lock(&g_vpad_mutex);
    internal_slot_t *s = &g_slots[slot];
    if (s->status == VP_PENDING) {
        s->remove_when_found = 1;
        pthread_mutex_unlock(&g_vpad_mutex);
        return;
    }

    int32_t handle = s->handle;
    int32_t alt_handle = s->alt_handle;
    uint64_t dev_id = s->device_id;
    int was_ready = (s->status == VP_READY);
    memset(s, 0, sizeof(*s));

    int any_live = 0;
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (g_slots[i].status != VP_FREE) { any_live = 1; break; }
    }
    pthread_mutex_unlock(&g_vpad_mutex);

#ifdef __PROSPERO__
    if (was_ready) {
        if (handle > 0) scePadVirtualDeviceDeleteDevice(handle);
        if (alt_handle > 0 && alt_handle != handle) scePadVirtualDeviceDeleteDevice(alt_handle);
        if (dev_id) shellui_remote_disconnect_device(dev_id);
        log_line("vpad: slot %d removed (handle %d, dev 0x%llx)", slot, handle, (unsigned long long)dev_id);
        notify_ps5("OmniPad: Controller disconnected (Slot %d)", slot + 1);
    }

    if (!any_live) {
        /* Sweep all potential virtual device handles 1..63 to ensure no phantom pad blocks the native controller */
        for (int h = 1; h < 64; h++) {
            scePadVirtualDeviceDeleteDevice(h);
        }
        /* Lower privilege to 0 so the native PS5 DualSense controller recovers 100% control */
        scePadSetProcessPrivilege(0);
        log_line("vpad: all slots cleared — privilege lowered (native pad restored)");
    }
#endif
}

int vpad_is_live(int slot)
{
    if (slot < 0 || slot >= MAX_SLOTS) return 0;
    return g_slots[slot].status == VP_READY;
}

int vpad_slot_is_free(int slot)
{
    if (slot < 0 || slot >= MAX_SLOTS) return 0;
    return g_slots[slot].status == VP_FREE;
}

void vpad_update(int slot, const pad_state_t *st)
{
    if (!st || slot < 0 || slot >= MAX_SLOTS) return;

    pthread_mutex_lock(&g_vpad_mutex);
    if (g_slots[slot].status != VP_READY || (g_slots[slot].handle <= 0 && g_slots[slot].alt_handle <= 0)) {
        pthread_mutex_unlock(&g_vpad_mutex);
        return;
    }
    g_slots[slot].battery_level = st->battery_level;
    g_slots[slot].battery_charging = st->battery_charging;
    g_slots[slot].last_update_time = now_ms();
    
    /* Latch state; vpad_poll handles continuous 250Hz injection */
    g_slots[slot].pad_state = *st;
    pthread_mutex_unlock(&g_vpad_mutex);
}

void vpad_press_ps_button(int slot)
{
    if (slot < 0 || slot >= MAX_SLOTS) return;
    pthread_mutex_lock(&g_vpad_mutex);
    int32_t handle = g_slots[slot].handle;
    pthread_mutex_unlock(&g_vpad_mutex);

    /* 1. Remote press inside SceShellUI */
    shellui_press_ps_button(handle);

    /* 2. Also inject via local vpad for 100ms */
    pad_state_t st;
    pad_state_neutral(&st);
    st.buttons = PAD_BTN_PS;
    vpad_update(slot, &st);
    usleep(100000);
    pad_state_neutral(&st);
    vpad_update(slot, &st);
    log_line("vpad: Synthesized PS button pressed on slot %d", slot + 1);
}

void vpad_get_slot_info(int slot, vpad_slot_info_t *out)
{
    if (slot < 0 || slot >= MAX_SLOTS || !out) return;
    pthread_mutex_lock(&g_vpad_mutex);
    out->status = g_slots[slot].status;
    out->handle = g_slots[slot].handle;
    out->alt_handle = g_slots[slot].alt_handle;
    out->device_id = g_slots[slot].device_id;
    out->user_id = g_slots[slot].user_id;
    out->conn_type = g_slots[slot].conn_type;
    snprintf(out->name, sizeof(out->name), "%s", g_slots[slot].name);
    out->battery_level = g_slots[slot].battery_level;
    out->battery_charging = g_slots[slot].battery_charging;
    out->packets_injected = g_slots[slot].packets_injected;
    out->connected_time = g_slots[slot].connected_time;
    out->last_update_time = g_slots[slot].last_update_time;
    pthread_mutex_unlock(&g_vpad_mutex);
}

int vpad_rebind_user(int slot, int32_t user_id)
{
    if (slot < 0 || slot >= MAX_SLOTS) return 0;
    pthread_mutex_lock(&g_vpad_mutex);
    internal_slot_t *s = &g_slots[slot];
    if (s->status != VP_READY) {
        pthread_mutex_unlock(&g_vpad_mutex);
        return 0;
    }

    int32_t target_user = user_id;
#ifdef __PROSPERO__
    if (target_user <= 0) {
        /* Cycle to next logged in user */
        int32_t list[4] = {-1, -1, -1, -1};
        if (sceUserServiceGetLoginUserIdList(list) == 0) {
            /* If current user is list[0], switch to list[1] (if logged in), else list[0] */
            if (s->user_id == list[0] && list[1] > 0) {
                target_user = list[1];
            } else if (s->user_id == list[1] && list[2] > 0) {
                target_user = list[2];
            } else if (list[0] > 0) {
                target_user = list[0];
            }
        }
        if (target_user <= 0) {
            sceUserServiceGetForegroundUser(&target_user);
        }
    }
#else
    if (target_user <= 0) target_user = 0x12611170;
#endif
    if (target_user <= 0) target_user = 0x10000000 + slot;

    s->user_id = target_user;
    uint64_t dev_id = s->device_id;
    int32_t handle = s->handle;
    pthread_mutex_unlock(&g_vpad_mutex);

    log_line("vpad: Rebinding slot %d to user 0x%08x (dev 0x%llx)",
             slot, (unsigned)target_user, (unsigned long long)dev_id);

    int bound = -1;
    if (dev_id > 0) {
        if (g_bind) bound = g_bind(dev_id, target_user);
        if (bound != 0) bound = shellui_remote_bind_device(dev_id, target_user);
    }
    if (handle > 0) {
        shellui_press_ps_button(handle);
    }

    notify_ps5("OmniPad: Slot %d -> Profile 0x%08x", slot + 1, (unsigned)target_user);
    return 1;
}

void vpad_cleanup_all(void)
{
    for (int i = 0; i < MAX_SLOTS; i++) {
        vpad_remove(i);
    }
    restore_privileges();
    g_ready = 0;
}

