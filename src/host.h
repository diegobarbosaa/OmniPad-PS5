/* Bluetooth HID host for game controllers, on a controller shared with the
 * console's own driver.
 *
 * Runs on one thread: host_poll() pumps the transport, dispatches what
 * arrived and advances every link's state machine. Every step that needs a
 * reply is retried or timed out, since a few packets go to the system's
 * driver instead of here.
 *
 *   pairing:    host_pair() searches for a pad in pairing mode, connects,
 *               pairs, reads its vendor/product id over SDP and opens the
 *               HID channels. The link key and ids are stored.
 *   reconnect:  a stored pad that is switched on connects by itself; it is
 *               accepted, authenticated with the stored key and its HID
 *               channels accepted or opened.
 */
#ifndef PH_HOST_H
#define PH_HOST_H

#include "hci.h"
#include "pad.h"

#define HOST_MAX_PADS 4

typedef struct host host_t;

typedef struct {
    void (*on_connect)(void *ud, int slot, const pad_info *info);
    void (*on_state)(void *ud, int slot, const pad_state *st);
    void (*on_disconnect)(void *ud, int slot);
    void *ud;
    /* Called every time the host waits (at least every 50 ms), including
     * while it waits for the controller to answer at start-up, so that the
     * menu keeps serving. Must be quick and must not call into the host.
     * NULL: nothing to do. */
    void (*idle)(void *ud);
    /* OmniPad extension: fires for every gamepad-class device the inquiry
     * finds (regardless of whether it ends up pairing). class_of_device is
     * the raw 24-bit Bluetooth CoD; NULL for the eir buffer when none was
     * supplied. */
    void (*on_discover)(void *ud, const unsigned char addr[6], uint32_t cod);
} host_events;

/* OmniPad extension: if set to a non-zero MAC, host_pair() still runs the
 * normal inquiry, but on_found() refuses every discovered device whose MAC
 * doesn't match. Pass {0,0,0,0,0,0} to clear and accept any gamepad. */
void host_set_allowed_mac(host_t *h, const unsigned char mac[6]);

/* Prepares the controller and loads the paired pads from db_path. Returns
 * NULL if the controller does not answer. */
host_t *host_open(hci_t hci, const char *db_path, const host_events *ev);
void    host_close(host_t *h);

void host_poll(host_t *h, int timeout_ms);

/* Searches for pads in pairing mode for `seconds`. */
int  host_pair(host_t *h, int seconds);

/* Sends rumble and lights to the pad in `slot`. Returns 0 if it is not
 * connected or takes no output. */
int  host_set_output(host_t *h, int slot, const pad_output *out);

/* Rumbles the pad in `slot` at strong/weak (0..255) for `ms` (50..5000),
 * keeping its lights, then stops it. Returns 0 if it is not connected or
 * has no rumble. For checking a profile's rumble from the page. */
int  host_rumble_test(host_t *h, int slot, uint8_t strong, uint8_t weak, int ms);

/* Game rumble for the pad in `slot` (0..255 each), keeping its lights. Sent
 * only when it changes; left alone while a test from the page runs.
 * Returns 0 if the pad is not connected or has no rumble. */
int  host_set_rumble(host_t *h, int slot, uint8_t strong, uint8_t weak);

int  host_connected(const host_t *h, int slot);
int  host_paired_count(const host_t *h);

/* The controller went away (rest mode): close and open again. */
int  host_transport_lost(const host_t *h);

/* ---- for the web page ---- */

typedef struct {
    unsigned char addr[6];      /* identity address */
    uint16_t vid, pid;
    int le;                     /* Bluetooth LE */
    int connected;
} host_paired_info;

/* Seconds left in the pairing window, 0 if not pairing. */
int  host_pairing_left(const host_t *h);

/* Seconds until a search that waits for a pause for the system starts; 0 if none waits. */
int  host_pair_wait_left(const host_t *h);
/* Seconds left of the pause for the system (usually the DualSense), 0 if none. */
int  host_pause_left(const host_t *h);

/* Leaves the chip to the system for the pause (events and, with yield_acl,
 * ACL data), so a DualSense being switched on can reconnect. Pads of ours
 * freeze meanwhile. Returns 0 if not possible now (a pad being linked). */
int  host_release_system(host_t *h);

/* The i-th paired pad (0 <= i < host_paired_count). Returns 0 past the end. */
int  host_paired_get(const host_t *h, int i, host_paired_info *out);

/* A connected, ready pad's identity and latest state. Returns 0 if none. */
int  host_pad_get(const host_t *h, int slot, pad_info *info, pad_state *st);

/* The settings of the pad in `slot`'s profile, as a JSON object (length, or
 * 0 if it has none), and setting one of them (saved for its model). */
int  host_pad_config(const host_t *h, int slot, char *out, int n);
int  host_pad_config_set(host_t *h, int slot, const char *key, const char *val);

/* OmniPad: pages a stored (paired) classic pad, for a pad that does not
 * call the console by itself, e.g. one last used with another device.
 * It is authenticated with its stored key like a pad that reconnects.
 * Returns 1 if the page was sent, 0 if the address is not paired, -1 if
 * not possible now (a search or another link is being set up, LE pad, or
 * it is already linked). */
int  host_recall(host_t *h, const unsigned char addr[6]);
/* OmniPad: re-reads the saved settings of every connected pad. */
int  host_pad_reload(host_t *h);
/* 1 if a link to addr exists in any state (paging, setting up, ready). */
int  host_link_exists(const host_t *h, const unsigned char addr[6]);
/* 1 if addr is in the paired store. */
int  host_is_paired(const host_t *h, const unsigned char addr[6]);

/* Forgets a paired pad: disconnects it if linked and drops its keys.
 * Returns 0 if it was not paired. */
int  host_forget(host_t *h, const unsigned char addr[6]);
/* Forgets every paired pad (a clean session). Returns how many. */
int  host_forget_all(host_t *h);

/* Dialog mode (web /api/tune and /api/hci). Changes a setting while running:
 * returns 1, or 0 for an unknown name or a value out of range. */
int  host_tune_set(host_t *h, const char *name, long value);
/* The settings, as JSON. */
int  host_tune_json(char *out, int n);
/* Sends one HCI command (never a Reset) and waits up to 2 s for its Command
 * Complete: returns 1 and its return parameters (status first) in reply. */
int  host_raw_cmd(host_t *h, unsigned op, const unsigned char *p, int plen, unsigned char *reply, int *rlen);

#endif
