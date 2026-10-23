/* OmniPad BT facade — implementation.
 *
 * This file is the bridge between OmniPad's slot/pad data model (pad_state_t,
 * vpad_add/update/remove) and AnyPad's host_t Bluetooth state machine.
 *
 * The AnyPad stack publishes three callbacks when something happens on a
 * pad: on_connect (new link up), on_state (an input report arrived) and
 * on_disconnect. We translate each one into the OmniPad slot API.
 *
 * Button bitmasks between the two state structs are identical by
 * construction (both derive from the PS5 DualSense standard: PAD_CROSS=0x4000,
 * PAD_L3=0x02, PAD_TOUCHPAD=0x100000, …), so the translation is a straight
 * field copy; see translate_state().
 */
#include "bt_host.h"
#include "host.h"
#include "hci.h"
#include "hci_usb.h"
#include "pad.h"
#include "ps5_vpad.h"
#include "ps5_haptic.h"
#include "profiles.h"
#include "usb_hotplug.h"
#include "remap.h"
#include "log.h"
#include "util.h"

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Paths. Keep the AnyPad bond-database location so an existing user can
 * re-use their saved pairings. */
#define BT_DB_PATH "/data/anypad/pads.db"

#define DISCO_MAX 16
#define DISCO_TTL_MS 90000

typedef struct {
    pthread_mutex_t mu;
    hci_t   transport;
    host_t *host;
    int     up;
    int     transport_open;

    /* slot i owns bt_slot_mac[i] when live[i] is non-zero. */
    int           live[HOST_MAX_PADS];
    unsigned char mac[HOST_MAX_PADS][6];
    pad_info      info[HOST_MAX_PADS];

    /* Per-controller small (weak) motor, from <map dir>/AABBCCDDEEFF.map
     * "small_motor = on|off". On unless the file turns it off. */
    int           small_motor[HOST_MAX_PADS];

    /* Rolling scan list — populated by on_discover during a pair window. */
    bt_discovered_t disco[DISCO_MAX];
    int             disco_n;
} bt_state_t;

static bt_state_t g;

/* ---- Reconnect job (OmniPad) --------------------------------------------- */

/* Store file layout, mirrored from host.c (DB_MAGIC / DB_REC / DB_MAX). */
#define STORE_MAGIC "ANYPAD02"
#define STORE_REC   64
#define STORE_MAX   16

#define RC_CALL_MS    30000     /* how long we keep calling the pad */
#define RC_RETRY_MS    1500     /* gap between pages that got no answer */
#define RC_REPAIR_S      90     /* pairing window opened as the fallback */

enum { RC_IDLE = 0, RC_CALLING, RC_REPAIRING, RC_CONNECTED, RC_FAILED };
static const char *const k_rc_phase[] = { "idle", "calling", "repairing", "connected", "failed" };

static struct {
    int           phase;
    unsigned char mac[6];
    long          started, until, next_try;
    int           attempts;
    const char   *reason;       /* why it moved to repairing / failed */
    long          done_at;      /* when it reached connected/failed (shown for a while) */
} g_rc;

static void rc_stop_locked(void)
{
    if (g_rc.phase == RC_CALLING || g_rc.phase == RC_REPAIRING) {
        g_rc.phase = RC_FAILED;
        g_rc.reason = "radio_released";
        g_rc.done_at = now_ms();
    }
}

static int rc_is_live(const unsigned char mac[6])
{
    for (int i = 0; i < HOST_MAX_PADS; i++)
        if (g.live[i] && memcmp(g.mac[i], mac, 6) == 0) return 1;
    return 0;
}

static void rc_finish(int phase, const char *reason)
{
    static const unsigned char zero[6] = {0};
    g_rc.phase = phase;
    g_rc.reason = reason;
    g_rc.done_at = now_ms();
    if (g.host) host_set_allowed_mac(g.host, zero);
    log_line("bt: reconnect %02X:%02X:%02X:%02X:%02X:%02X: %s%s%s", g_rc.mac[5], g_rc.mac[4], g_rc.mac[3],
             g_rc.mac[2], g_rc.mac[1], g_rc.mac[0], k_rc_phase[phase], reason ? " - " : "", reason ? reason : "");
}

static void rc_step(long now)
{
    if (!g.up || !g.host) return;
    if (g_rc.phase == RC_CALLING) {
        if (rc_is_live(g_rc.mac)) { rc_finish(RC_CONNECTED, NULL); return; }
        int key_lost = !host_is_paired(g.host, g_rc.mac);   /* host drops it on auth failure */
        int timed_out = now > g_rc.until && !host_link_exists(g.host, g_rc.mac);
        if (key_lost || timed_out) {
            g_rc.reason = key_lost ? "key_lost" : "no_answer";
            g_rc.phase = RC_REPAIRING;
            host_set_allowed_mac(g.host, g_rc.mac);
            bt_host_start_pairing(RC_REPAIR_S);
            log_line("bt: reconnect: %s, opening a %d s pairing window for this pad",
                     g_rc.reason, RC_REPAIR_S);
            return;
        }
        if (now >= g_rc.next_try && now <= g_rc.until && !host_link_exists(g.host, g_rc.mac)) {
            int r = host_recall(g.host, g_rc.mac);
            if (r == 1) g_rc.attempts++;
            g_rc.next_try = now + RC_RETRY_MS;
        }
    } else if (g_rc.phase == RC_REPAIRING) {
        if (rc_is_live(g_rc.mac)) { rc_finish(RC_CONNECTED, NULL); return; }
        if (!bt_host_is_pairing()) rc_finish(RC_FAILED, g_rc.reason);
    }
}

static void bt_atexit_cleanup(void);

/* ---- state translation ------------------------------------------------- */

/* AnyPad's pad_state → OmniPad's pad_state_t. Button bits are already
 * identical; what changes is the shape of touch/motion/battery metadata. */
static void translate_state(pad_state_t *out, const pad_state *in,
                            pad_conn_type_t conn, const pad_info *info)
{
    pad_state_neutral(out);
    out->buttons = in->buttons;
    out->lx = in->lx; out->ly = in->ly;
    out->rx = in->rx; out->ry = in->ry;
    out->l2 = in->l2; out->r2 = in->r2;
    if (in->has_motion) {
        out->accel[0] = in->accel[0]; out->accel[1] = in->accel[1]; out->accel[2] = in->accel[2];
        out->gyro[0]  = in->gyro[0];  out->gyro[1]  = in->gyro[1];  out->gyro[2]  = in->gyro[2];
    }
    if (in->has_touch) {
        out->touch_down = in->touch[0].down;
        out->touch_x = in->touch[0].x;
        out->touch_y = in->touch[0].y;
    }
    if (in->has_battery) {
        out->battery_level    = in->battery_pct;
        out->battery_charging = in->charging;
    }
    out->connected = 1;
    out->conn_type = conn;
    if (info) {
        out->vid = info->vid;
        out->pid = info->pid;
        memcpy(&out->mac_or_dev_id, info->addr, 6);
    }
}

/* ---- callbacks from host_t --------------------------------------------- */

/* Reads "small_motor" from this controller's own settings file. Default on:
 * pads without a file rumble exactly as before. */
static int read_small_motor(const unsigned char mac[6])
{
    char path[300], line[128];
    int on = 1;
    snprintf(path, sizeof path, "%s/%02X%02X%02X%02X%02X%02X.map", BT_MAP_DIR,
             mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
    FILE *f = fopen(path, "r");
    if (!f) return 1;
    while (fgets(line, sizeof line, f)) {
        char *k = line, *eq = strchr(line, '=');
        if (!eq || *k == '#') continue;
        *eq = 0;
        while (*k == ' ' || *k == '\t') k++;
        char *ke = eq; while (ke > k && (ke[-1] == ' ' || ke[-1] == '\t')) *--ke = 0;
        if (strcmp(k, "small_motor") != 0) continue;
        char *v = eq + 1; while (*v == ' ' || *v == '\t') v++;
        on = strncmp(v, "off", 3) != 0;
    }
    fclose(f);
    return on;
}

/* This controller's button overrides, from <map dir>/AABBCCDDEEFF.map. */
static void bt_apply_remap(int slot, const unsigned char mac[6])
{
    char id[16];
    uint32_t map[REMAP_N];
    snprintf(id, sizeof id, "%02X%02X%02X%02X%02X%02X", mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
    remap_load(id, map);
    vpad_set_remap(slot, map);
}

static void on_connect(void *ud, int slot, const pad_info *info)
{
    char name[64];
    (void)ud;
    if (slot < 0 || slot >= HOST_MAX_PADS) return;

    int small = read_small_motor(info->addr);
    pthread_mutex_lock(&g.mu);
    g.live[slot] = 1;
    g.small_motor[slot] = small;
    memcpy(g.mac[slot], info->addr, 6);
    g.info[slot] = *info;
    pthread_mutex_unlock(&g.mu);

    snprintf(name, sizeof name, "%s (%04x:%04x)",
             info->profile ? info->profile : "BT pad", info->vid, info->pid);
    log_line("bt: slot %d connected %02x:%02x:%02x:%02x:%02x:%02x [%s]",
             slot, info->addr[5], info->addr[4], info->addr[3],
             info->addr[2], info->addr[1], info->addr[0], name);

    if (!vpad_add(slot, CONN_BLUETOOTH_CLASSIC, name))
        log_line("bt: slot %d vpad_add failed", slot);
    bt_apply_remap(slot, info->addr);
}

static void on_state(void *ud, int slot, const pad_state *st)
{
    pad_state_t out;
    (void)ud;
    if (slot < 0 || slot >= HOST_MAX_PADS) return;
    pthread_mutex_lock(&g.mu);
    pad_info info = g.info[slot];
    int live = g.live[slot];
    pthread_mutex_unlock(&g.mu);
    if (!live) return;
    translate_state(&out, st, CONN_BLUETOOTH_CLASSIC, &info);
    vpad_update(slot, &out);
}

static void on_disconnect(void *ud, int slot)
{
    (void)ud;
    if (slot < 0 || slot >= HOST_MAX_PADS) return;
    log_line("bt: slot %d disconnected", slot);
    pthread_mutex_lock(&g.mu);
    g.live[slot] = 0;
    pthread_mutex_unlock(&g.mu);
    vpad_remove(slot);
}

static void idle_cb(void *ud)
{
    (void)ud;
    vpad_poll(now_ms());
}

/* Called from host.c for every gamepad-class device the inquiry sees. */
static void on_discover(void *ud, const unsigned char addr[6], uint32_t cod)
{
    long now = now_ms();
    (void)ud;
    pthread_mutex_lock(&g.mu);
    /* Age out stale rows first. */
    int w = 0;
    for (int r = 0; r < g.disco_n; r++) {
        if (now - g.disco[r].last_seen_ms < DISCO_TTL_MS) {
            if (w != r) g.disco[w] = g.disco[r];
            w++;
        }
    }
    g.disco_n = w;
    /* Update or insert this one. */
    int idx = -1;
    for (int i = 0; i < g.disco_n; i++)
        if (memcmp(g.disco[i].mac, addr, 6) == 0) { idx = i; break; }
    if (idx < 0 && g.disco_n < DISCO_MAX) {
        idx = g.disco_n++;
        memcpy(g.disco[idx].mac, addr, 6);
        g.disco[idx].first_seen_ms = now;
        g.disco[idx].cod = cod;
    }
    if (idx >= 0) {
        g.disco[idx].last_seen_ms = now;
        g.disco[idx].cod = cod;
    }
    pthread_mutex_unlock(&g.mu);
    log_line("bt: discovered %02x:%02x:%02x:%02x:%02x:%02x cod=%06x",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0], cod);
}

int bt_host_discovered(bt_discovered_t *out, int max)
{
    int n;
    pthread_mutex_lock(&g.mu);
    n = g.disco_n < max ? g.disco_n : max;
    for (int i = 0; i < n; i++) out[i] = g.disco[i];
    pthread_mutex_unlock(&g.mu);
    return n;
}

/* ---- Haptic-audio game-rumble bridge --------------------------------- */

static int   g_haptic_enabled = 0;
static int   g_haptic_started = 0;
/* Runtime-tunable knobs (dashboard /api/bt/haptic/settings). */
static float g_haptic_gain    = 1.0f;    /* multiplier applied to each motor level before send */
static int   g_haptic_eager   = 1;       /* 1: open the Avcap2 stream as soon as the toggle is on,
                                          * even without a connected pad, so the first rumble has
                                          * no cold-start delay. 0: only while a pad is live. */

void bt_haptic_enable(int on)
{
    g_haptic_enabled = on ? 1 : 0;
    if (g_haptic_enabled && !g_haptic_started) {
        g_haptic_started = (haptic_start() != 0);
        if (g_haptic_started) log_line("bt: haptic-audio bridge started");
        else log_line("bt: haptic-audio bridge could not start (libSceAvcap2 unavailable?)");
    }
    if (!g_haptic_enabled) haptic_want(0);
    else if (g_haptic_eager) haptic_want(1);     /* keep stream warm; no cold-start on first pad */
}

int bt_haptic_is_enabled(void) { return g_haptic_enabled; }

void bt_haptic_settings_get(float *gain, int *eager)
{
    if (gain)  *gain  = g_haptic_gain;
    if (eager) *eager = g_haptic_eager;
}
void bt_haptic_settings_set(float gain, int eager)
{
    if (gain >= 0.1f && gain <= 4.0f) g_haptic_gain = gain;
    g_haptic_eager = eager ? 1 : 0;
    if (g_haptic_enabled && g_haptic_started) haptic_want(g_haptic_eager);
}

static uint8_t apply_gain(uint8_t v, float gain)
{
    float o = (float)v * gain;
    if (o > 255.0f) return 255;
    if (o < 0.0f)   return 0;
    return (uint8_t)(o + 0.5f);
}

void bt_haptic_pump(void)
{
    static int usb_zeroed = 1;
    if (!g_haptic_enabled || !g_haptic_started) {
        /* Bridge off: make sure USB pads are left still. */
        if (!usb_zeroed) { for (int i = 0; i < MAX_SLOTS; i++) usb_hotplug_set_rumble(i, 0, 0); usb_zeroed = 1; }
        return;
    }
    int any = usb_hotplug_any();
    if (g.up) for (int i = 0; i < HOST_MAX_PADS; i++) if (g.live[i]) { any = 1; break; }
    /* Eager mode keeps the stream warm regardless of pad presence so there's
     * no 1-2 s Avcap2 cold-start when a game first rumbles. */
    haptic_want(g_haptic_eager ? 1 : any);
    if (!any) return;
    uint8_t s = 0, w = 0;
    if (!haptic_levels(&s, &w)) return;
    s = apply_gain(s, g_haptic_gain);
    w = apply_gain(w, g_haptic_gain);
    /* Send every tick; host_set_rumble is already a change-only transport
     * for the actual over-air write, and sending the current level each
     * tick lets small-amplitude transitions through promptly. */
    if (g.up) {
        for (int i = 0; i < HOST_MAX_PADS; i++) {
            if (!g.live[i]) continue;
            host_set_rumble(g.host, i, s, g.small_motor[i] ? w : 0);
        }
    }
    /* USB pads: the reader thread applies that pad's small-motor setting. */
    for (int i = 0; i < MAX_SLOTS; i++) usb_hotplug_set_rumble(i, s, w);
    usb_zeroed = 0;
}

int bt_host_pair_mac(const unsigned char mac[6], int seconds)
{
    static const unsigned char zero[6] = {0};
    if (!mac || memcmp(mac, zero, 6) == 0) return 0;
    if (!bt_host_is_up() && !bt_host_init()) return 0;
    pthread_mutex_lock(&g.mu);
    if (g.host) host_set_allowed_mac(g.host, mac);
    pthread_mutex_unlock(&g.mu);
    if (seconds <= 0 || seconds > 300) seconds = 120;
    bt_host_start_pairing(seconds);
    return 1;
}

/* ---- public API --------------------------------------------------------- */

int bt_host_init(void)
{
    pthread_mutex_lock(&g.mu);
    if (g.up) { pthread_mutex_unlock(&g.mu); return 1; }

    if (!hci_usb_open(&g.transport)) {
        log_line("bt: HCI transport unavailable (chip busy or not a known BT device)");
        pthread_mutex_unlock(&g.mu);
        return 0;
    }
    g.transport_open = 1;

    host_events ev = {
        .on_connect    = on_connect,
        .on_state      = on_state,
        .on_disconnect = on_disconnect,
        .idle          = idle_cb,
        .on_discover   = on_discover,
        .ud            = NULL,
    };
    g.host = host_open(g.transport, BT_DB_PATH, &ev);
    if (!g.host) {
        log_line("bt: host_open failed (controller did not answer)");
        /* hci_usb_open opened the fd; host_open normally closes transport on
         * failure, but be defensive: mark it closed either way. */
        g.transport_open = 0;
        pthread_mutex_unlock(&g.mu);
        return 0;
    }

    /* host_open points the profiles at <db dir>/maps (/data/anypad/maps); the
     * dashboard saves to BT_MAP_DIR, so point them back there. */
    profiles_set_map_dir(BT_MAP_DIR);

    g.up = 1;
    log_line("bt: host up — %d paired pad(s) loaded", host_paired_count(g.host));
    pthread_mutex_unlock(&g.mu);

    /* Register the atexit hook once so an abnormal (but not SIGKILL) exit
     * still restores the chip. Multiple registrations are harmless but we
     * guard anyway. */
    static int atexit_hooked = 0;
    if (!atexit_hooked) { atexit(bt_atexit_cleanup); atexit_hooked = 1; }

    return 1;
}

static int  g_idle_release_minutes = 5;
static long g_last_pad_seen_ms = 0;

void bt_host_idle_release_set(int minutes)
{
    if (minutes < 0) minutes = 0;
    if (minutes > 240) minutes = 240;
    g_idle_release_minutes = minutes;
}
int bt_host_idle_release_get(void) { return g_idle_release_minutes; }

void bt_host_poll(long now)
{
    if (!g.up) return;
    host_poll(g.host, 0);
    if (host_transport_lost(g.host)) {
        log_line("bt: transport lost — closing");
        bt_host_cleanup();
        return;
    }
    /* Track last activity: if any pad is live right now, reset the clock. */
    int any = 0;
    for (int i = 0; i < HOST_MAX_PADS; i++) if (g.live[i]) { any = 1; break; }
    if (any) g_last_pad_seen_ms = now;
    else if (g_last_pad_seen_ms == 0) g_last_pad_seen_ms = now;
    rc_step(now);
    if (!g.up) return;                  /* rc_step never closes the radio, but stay safe */
    if (g_rc.phase == RC_CALLING || g_rc.phase == RC_REPAIRING) g_last_pad_seen_ms = now;
    /* If no pad connected for N minutes and no pairing window is open, close
     * BT so the system BT daemon can page-scan freely for a native
     * DualSense reconnect. User re-opens via dashboard. */
    if (g_idle_release_minutes > 0 && !any && !bt_host_is_pairing() &&
        now - g_last_pad_seen_ms > g_idle_release_minutes * 60L * 1000L) {
        log_line("bt: idle for %d min — releasing chip so system can reconnect DualSense",
                 g_idle_release_minutes);
        bt_host_cleanup();
    }
}

void bt_host_start_pairing(int duration_sec)
{
    if (!g.up) { log_line("bt: start_pairing: host not up"); return; }
    if (duration_sec <= 0) duration_sec = 60;
    if (duration_sec > 300) duration_sec = 300;
    if (host_pair(g.host, duration_sec))
        log_line("bt: pairing window open for %ds", duration_sec);
    else
        log_line("bt: pairing failed to start");
}

int bt_host_is_pairing(void)
{
    if (!g.up) return 0;
    return host_pairing_left(g.host) > 0;
}

int bt_host_get_pairing_seconds_left(void)
{
    if (!g.up) return 0;
    return host_pairing_left(g.host);
}

void bt_host_disconnect_device(int slot)
{
    if (!g.up || slot < 0 || slot >= HOST_MAX_PADS) return;
    /* Mark the slot dead; the host will trigger on_disconnect which does
     * vpad_remove. For an immediate hard disconnect we would need a
     * host_disconnect(slot) API that AnyPad doesn't expose; for now, let
     * the next pad-side request close it, or wait for a transport reset. */
    pthread_mutex_lock(&g.mu);
    g.live[slot] = 0;
    pthread_mutex_unlock(&g.mu);
    vpad_remove(slot);
}

void bt_host_cleanup(void)
{
    pthread_mutex_lock(&g.mu);
    if (!g.up && !g.transport_open) { pthread_mutex_unlock(&g.mu); return; }
    for (int i = 0; i < HOST_MAX_PADS; i++) {
        if (g.live[i]) { vpad_remove(i); g.live[i] = 0; }
    }
    /* Also kill the haptic bridge if it was running — the Avcap2 service
     * holds a session that outlives our process if we don't close it. */
    haptic_want(0);

    host_t *to_close = g.host;
    g.host = NULL;
    pthread_mutex_unlock(&g.mu);

    if (to_close) host_close(to_close);
    /* host_close sends HCI_Disconnect for every open link and waits ~2s
     * for them to drop before returning. We add an extra 500ms settle so
     * the chip's internal connection table is fully freed before the
     * system reclaims the radio — if we exit while the chip still has
     * ghost ACL handles, a native DualSense trying to reconnect in the
     * next few seconds can hit a "no free slot" rejection and give up.
     * This is the single biggest cause of "need to reboot after stop". */
    usleep(500000);

    pthread_mutex_lock(&g.mu);
    g.up = 0;
    g.transport_open = 0;
    g.disco_n = 0;
    rc_stop_locked();
    pthread_mutex_unlock(&g.mu);
    log_line("bt: host cleaned up (500ms chip-settle complete)");
}

/* atexit() hook — fires on normal exit, abort(), or returned-from-main even
 * if the payload is killed with SIGTERM (which our handler converts to a
 * g_running=0 and a normal loop exit). It will NOT fire on SIGKILL / segfault
 * crashes; those cases still need a reboot. */
static void bt_atexit_cleanup(void)
{
    bt_host_cleanup();
}

int bt_host_is_up(void) { return g.up; }

const unsigned char *bt_host_slot_mac(int slot)
{
    if (slot < 0 || slot >= HOST_MAX_PADS || !g.live[slot]) return NULL;
    return g.mac[slot];
}

void bt_host_forget(const unsigned char mac[6])
{
    if (!mac) return;
    if (g_rc.phase != RC_IDLE && memcmp(g_rc.mac, mac, 6) == 0) bt_host_reconnect_cancel();
    if (g.up && g.host) {
        host_forget(g.host, mac);       /* disconnects it if linked, saves the store */
        return;
    }
    /* Radio released: edit the store file directly, same format as db_save. */
    unsigned char hdr[8], rec[STORE_REC];
    unsigned char keep[STORE_MAX][STORE_REC];
    int n = 0, dropped = 0;
    FILE *f = fopen(BT_DB_PATH, "rb");
    if (!f) return;
    if (fread(hdr, 1, 8, f) != 8 || memcmp(hdr, STORE_MAGIC, 8) != 0) { fclose(f); return; }
    while (n < STORE_MAX && fread(rec, 1, STORE_REC, f) == STORE_REC) {
        if (memcmp(rec, mac, 6) == 0) { dropped = 1; continue; }
        memcpy(keep[n++], rec, STORE_REC);
    }
    fclose(f);
    if (!dropped) return;
    char tmp[300];
    snprintf(tmp, sizeof tmp, "%s.new", BT_DB_PATH);
    f = fopen(tmp, "wb");
    if (!f) return;
    int ok = fwrite(hdr, 1, 8, f) == 8;
    for (int i = 0; i < n && ok; i++) ok = fwrite(keep[i], 1, STORE_REC, f) == STORE_REC;
    if (fclose(f) != 0) ok = 0;
    if (!ok || rename(tmp, BT_DB_PATH) != 0) { remove(tmp); log_line("bt: forget: could not rewrite store"); return; }
    log_line("bt: forgot %02X:%02X:%02X:%02X:%02X:%02X (store edited, radio released)",
             mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
}

int bt_host_slot_ids(int slot, uint16_t *vid, uint16_t *pid, unsigned char mac[6])
{
    int ok = 0;
    if (slot < 0 || slot >= HOST_MAX_PADS) return 0;
    pthread_mutex_lock(&g.mu);
    if (g.live[slot]) {
        *vid = g.info[slot].vid;
        *pid = g.info[slot].pid;
        memcpy(mac, g.mac[slot], 6);
        ok = 1;
    }
    pthread_mutex_unlock(&g.mu);
    return ok;
}

void bt_host_reload_configs(void)
{
    usb_hotplug_reload_configs();
    if (!g.up || !g.host) return;
    host_pad_reload(g.host);
    for (int i = 0; i < HOST_MAX_PADS; i++) {
        if (!g.live[i]) continue;
        g.small_motor[i] = read_small_motor(g.mac[i]);
        bt_apply_remap(i, g.mac[i]);
    }
}

int bt_host_scan(int seconds)
{
    static const unsigned char zero[6] = {0};
    if (!bt_host_is_up() && !bt_host_init()) return 0;
    if (g.host) host_set_allowed_mac(g.host, zero);
    bt_host_start_pairing(seconds);
    return 1;
}

static const char *pad_name(uint16_t vid, uint16_t pid)
{
    const pad_profile *p = profile_find(vid, pid);
    return p ? p->name : NULL;
}

int bt_host_paired(bt_paired_t *out, int max)
{
    int n = 0;
    if (g.up && g.host) {
        host_paired_info pi;
        for (int i = 0; n < max && host_paired_get(g.host, i, &pi); i++) {
            memcpy(out[n].mac, pi.addr, 6);
            out[n].vid = pi.vid; out[n].pid = pi.pid; out[n].le = pi.le;
            out[n].connected = pi.connected;
            out[n].name = pad_name(pi.vid, pi.pid);
            n++;
        }
        return n;
    }
    /* Radio released: read the store file (layout in host.c db_save). */
    unsigned char rec[STORE_REC];
    FILE *f = fopen(BT_DB_PATH, "rb");
    if (!f) return 0;
    if (fread(rec, 1, 8, f) != 8 || memcmp(rec, STORE_MAGIC, 8) != 0) { fclose(f); return 0; }
    while (n < max && fread(rec, 1, STORE_REC, f) == STORE_REC) {
        memcpy(out[n].mac, rec, 6);
        out[n].vid = (uint16_t)(rec[23] | rec[24] << 8);
        out[n].pid = (uint16_t)(rec[25] | rec[26] << 8);
        out[n].le = rec[27];
        out[n].connected = 0;
        out[n].name = pad_name(out[n].vid, out[n].pid);
        n++;
    }
    fclose(f);
    return n;
}

int bt_host_reconnect(const unsigned char mac[6])
{
    static const unsigned char zero[6] = {0};
    if (!mac || memcmp(mac, zero, 6) == 0) return 0;
    if (!bt_host_is_up() && !bt_host_init()) return -1;
    if (!host_is_paired(g.host, mac)) return 0;
    memcpy(g_rc.mac, mac, 6);
    g_rc.phase = rc_is_live(mac) ? RC_CONNECTED : RC_CALLING;
    g_rc.started = now_ms();
    g_rc.until = g_rc.started + RC_CALL_MS;
    g_rc.next_try = g_rc.started;
    g_rc.attempts = 0;
    g_rc.reason = NULL;
    g_rc.done_at = g_rc.phase == RC_CONNECTED ? g_rc.started : 0;
    g_last_pad_seen_ms = g_rc.started;
    log_line("bt: reconnect %02X:%02X:%02X:%02X:%02X:%02X started", mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
    return 1;
}

void bt_host_reconnect_cancel(void)
{
    if (g_rc.phase != RC_CALLING && g_rc.phase != RC_REPAIRING) return;
    rc_finish(RC_FAILED, "cancelled");
}

int bt_host_reconnect_json(char *out, int n)
{
    long now = now_ms();
    int left = 0;
    if (g_rc.phase == RC_CALLING) left = (int)((g_rc.until - now + 999) / 1000);
    else if (g_rc.phase == RC_REPAIRING) left = bt_host_get_pairing_seconds_left();
    if (left < 0) left = 0;
    /* A finished job is reported for 20 s, then the page sees idle again. */
    int phase = g_rc.phase;
    if ((phase == RC_CONNECTED || phase == RC_FAILED) && now - g_rc.done_at > 20000) phase = RC_IDLE;
    return snprintf(out, (size_t)n,
        "{\"phase\":\"%s\",\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"reason\":\"%s\",\"left\":%d,\"attempts\":%d}",
        k_rc_phase[phase], g_rc.mac[5], g_rc.mac[4], g_rc.mac[3], g_rc.mac[2], g_rc.mac[1], g_rc.mac[0],
        g_rc.reason ? g_rc.reason : "", left, g_rc.attempts);
}

__attribute__((constructor))
static void bt_state_init(void)
{
    pthread_mutex_init(&g.mu, NULL);
}
