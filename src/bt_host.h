/* OmniPad facade over AnyPad's BT host_t.
 *
 * Keeps the public API main.c already uses, but is backed by AnyPad's
 * mature BT stack (host.c + hci_usb.c + le.c + evstream.c + usb_desc.c +
 * smp_crypto.c). The old minimal skeleton in bt_host.c/bt_hci_usb.c has
 * been removed.
 *
 * Lifecycle:
 *   bt_host_init()   opens the Bluetooth chip and loads the paired-pads
 *                    database. Idempotent; returns 1 on success, 0 if the
 *                    chip is busy or missing.
 *   bt_host_poll(now)runs one iteration of the host state machine. Call
 *                    from the main loop every few ms.
 *   bt_host_start_pairing(seconds)  opens a pairing window; discovered
 *                    gamepads in pairing mode are paired and bonded.
 *   bt_host_cleanup()gracefully disconnects every pad, restores the chip
 *                    state it modified, and releases the chip. SAFE to call
 *                    even if bt_host_init() failed.
 */
#ifndef BT_HOST_H
#define BT_HOST_H

#include "pad_types.h"
#include <stdint.h>

int  bt_host_init(void);
void bt_host_poll(long now);
void bt_host_start_pairing(int duration_sec);
int  bt_host_is_pairing(void);
int  bt_host_get_pairing_seconds_left(void);
void bt_host_disconnect_device(int slot);
void bt_host_cleanup(void);

/* True once bt_host_init() has succeeded and the chip is open. */
int  bt_host_is_up(void);

/* MAC address of a currently connected BT slot, or NULL. */
const unsigned char *bt_host_slot_mac(int slot);

/* Forget a paired MAC (also disconnects it if currently linked). */
void bt_host_forget(const unsigned char mac[6]);

/* ---- Scan list (OmniPad extension) --------------------------------------
 * While a pairing window is open, every gamepad-class device discovered by
 * HCI inquiry is recorded here. The dashboard reads this list, renders the
 * discovered devices, and calls bt_host_pair_mac(mac) when the user picks
 * one. Devices older than ~90 s are evicted.
 */
typedef struct {
    unsigned char mac[6];
    uint32_t      cod;
    long          first_seen_ms;
    long          last_seen_ms;
} bt_discovered_t;

/* Snapshot the discovery list into caller buffer. Returns the number
 * written, up to `max`. Thread-safe (locks internally). */
int  bt_host_discovered(bt_discovered_t *out, int max);

/* Start a pair window scoped to one MAC. Returns 0 if BT could not be
 * brought up or `mac` is NULL / all-zero. */
int  bt_host_pair_mac(const unsigned char mac[6], int seconds);

/* Open a wide pairing window (no MAC filter). Brings the radio up first. */
int  bt_host_scan(int seconds);

/* ---- Remembered pads (OmniPad extension) --------------------------------
 * Pads paired earlier keep their link key in the store. A pad that is also
 * used with another device (phone, PC) usually calls whatever it was last
 * linked to, so it will not come back here by itself. Reconnect:
 *   1. brings the radio up and calls the pad for ~30 s with its stored key
 *      (the pad must be switched on and not linked elsewhere);
 *   2. if the pad does not answer, or answers but lost our key because it
 *      was paired with another device since, opens a 90 s pairing window
 *      for that pad only: hold its pairing button and it is paired afresh.
 * Works with the radio released too: the list is read from the store file. */
typedef struct {
    unsigned char mac[6];
    uint16_t vid, pid;
    int le;
    int connected;
    const char *name;           /* profile name, or NULL if unknown */
} bt_paired_t;

int  bt_host_paired(bt_paired_t *out, int max);

/* Where per-controller and per-model settings files live. */
#define BT_MAP_DIR "/data/omnipad/maps"
/* Identity of the pad in a live BT slot. Returns 0 if the slot is not a live BT pad. */
int  bt_host_slot_ids(int slot, uint16_t *vid, uint16_t *pid, unsigned char mac[6]);
/* Apply saved settings files to connected pads now (after the page saves one). */
void bt_host_reload_configs(void);
/* Returns 1 started, 0 not paired, -1 radio could not be brought up. */
int  bt_host_reconnect(const unsigned char mac[6]);
void bt_host_reconnect_cancel(void);
/* Reconnect progress as a JSON object into out; returns its length. */
int  bt_host_reconnect_json(char *out, int n);

/* ---- Game-rumble bridge (haptic-audio capture) --------------------------
 * Opt-in Avcap2 bridge: reads the haptic signal the console would send to a
 * DualSense's actuators (even for games that don't call scePadSetVibration)
 * and forwards it as rumble to every connected BT pad. Starts off; the
 * dashboard toggle flips bt_haptic_enable.
 */
void bt_haptic_enable(int on);     /* 1: start/keep the stream open while any pad is up; 0: shut it down */
int  bt_haptic_is_enabled(void);
void bt_haptic_pump(void);         /* called from the main loop every tick */

/* Tuning knobs:
 *   gain  — multiplier applied to each motor level before send (0.1..4.0, default 1.0)
 *   eager — 1 keeps the Avcap2 stream open even without a connected pad so the first
 *           rumble doesn't incur a 1-2 s cold-start (default). 0 = open only while
 *           a BT pad is up. */
void bt_haptic_settings_get(float *gain, int *eager);
void bt_haptic_settings_set(float gain, int eager);

/* ---- Idle auto-release -------------------------------------------------
 * If BT is up and no pad has been connected for `minutes`, close the chip
 * so the system BT daemon has clean access for incoming DualSense page
 * scans. 0 disables (keep BT open forever once brought up). Default 5. */
void bt_host_idle_release_set(int minutes);
int  bt_host_idle_release_get(void);

#endif /* BT_HOST_H */
