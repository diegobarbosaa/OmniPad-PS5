#include "bt_host.h"
#include "bt_hci_usb.h"
#include "bt_packets.h"
#include "ps5_vpad.h"
#include "profiles.h"
#include "log.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define PADS_DB_PATH "/data/anypad/pads.db"

typedef struct {
    int         pairing_active;
    long        pairing_end_time;
    bt_device_t devices[MAX_SLOTS];
    pthread_mutex_t lock;
} bt_host_state_t;

static bt_host_state_t g_host;

int bt_host_init(void)
{
    memset(&g_host, 0, sizeof(g_host));
    pthread_mutex_init(&g_host.lock, NULL);

    for (int i = 0; i < MAX_SLOTS; i++) {
        g_host.devices[i].slot = -1;
    }

    if (!bt_hci_init()) {
        log_line("bt_host: failed to initialize HCI interface");
        return 0;
    }

    log_line("bt_host: Bluetooth Host stack active");
    return 1;
}

void bt_host_start_pairing(int duration_sec)
{
    pthread_mutex_lock(&g_host.lock);
    g_host.pairing_active = 1;
    g_host.pairing_end_time = now_ms() + (long)duration_sec * 1000L;
    pthread_mutex_unlock(&g_host.lock);

    /* Send HCI Inquiry (LAP 0x9E8B33, Length 8, NumResponses 0) */
    uint8_t lap[3] = {0x33, 0x8B, 0x9E};
    uint8_t param[5];
    memcpy(param, lap, 3);
    param[3] = 0x08; /* length in 1.28s units */
    param[4] = 0x00; /* unlimited responses */
    bt_hci_send_cmd(0x0001, 0x01, param, sizeof(param));

    log_line("bt_host: started Bluetooth pairing discovery for %d seconds", duration_sec);
    notify_ps5("OmniPad: Scanning for Bluetooth controllers (%ds)...", duration_sec);
}

int bt_host_is_pairing(void)
{
    int active;
    pthread_mutex_lock(&g_host.lock);
    active = g_host.pairing_active;
    pthread_mutex_unlock(&g_host.lock);
    return active;
}

int bt_host_get_pairing_seconds_left(void)
{
    long end_time;
    pthread_mutex_lock(&g_host.lock);
    if (!g_host.pairing_active) {
        pthread_mutex_unlock(&g_host.lock);
        return 0;
    }
    end_time = g_host.pairing_end_time;
    pthread_mutex_unlock(&g_host.lock);
    long diff = end_time - now_ms();
    return diff > 0 ? (int)(diff / 1000L) : 0;
}

void bt_host_disconnect_device(int slot)
{
    if (slot < 0 || slot >= MAX_SLOTS) return;

    pthread_mutex_lock(&g_host.lock);
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (g_host.devices[i].slot == slot && g_host.devices[i].connected) {
            uint16_t handle = g_host.devices[i].acl_handle;
            if (handle) {
                uint8_t param[3];
                param[0] = (uint8_t)(handle & 0xFF);
                param[1] = (uint8_t)(handle >> 8);
                param[2] = 0x13; /* Remote User Terminated Connection */
                bt_hci_send_cmd(0x0006, 0x01, param, sizeof(param));
            }
            g_host.devices[i].connected = 0;
            g_host.devices[i].slot = -1;
            break;
        }
    }
    pthread_mutex_unlock(&g_host.lock);

    vpad_remove(slot);
}

void bt_host_poll(long now)
{
    bt_hci_poll();

    /* Process pairing timeout */
    pthread_mutex_lock(&g_host.lock);
    int pairing_expired = g_host.pairing_active && now >= g_host.pairing_end_time;
    if (pairing_expired) g_host.pairing_active = 0;
    pthread_mutex_unlock(&g_host.lock);
    if (pairing_expired) {
        /* Cancel Inquiry */
        bt_hci_send_cmd(0x0002, 0x01, NULL, 0);
        log_line("bt_host: pairing window closed");
        notify_ps5("OmniPad: Bluetooth scanning complete");
    }

    /* Process HCI events */
    uint8_t ev[HCI_PKT_MAX];
    int len;
    while ((len = bt_hci_recv_event(ev, sizeof(ev))) > 0) {
        if (len < 2) continue;
        uint8_t ev_code = ev[0];
        uint8_t param_len = ev[1];
        if ((size_t)param_len != (size_t)(len - 2)) continue;
        const uint8_t *param = ev + 2;

        /* Inquiry Result or Extended Inquiry Result */
        if (ev_code == 0x02 || ev_code == 0x2F) {
            /* Look for gamepad device and initiate connection if pairing */
            if (bt_host_is_pairing() && param_len >= 14) {
                const uint8_t *mac = param + 1;
                log_line("bt_host: discovered device %02x:%02x:%02x:%02x:%02x:%02x",
                         mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
            }
        }
        /* Connection Complete */
        else if (ev_code == 0x03 && param_len >= 11) {
            uint8_t status = param[0];
            uint16_t handle = (uint16_t)(param[1] | (param[2] << 8));
            if (status == 0) {
                log_line("bt_host: connected to device (handle 0x%04x)", handle);
            }
        }
        /* Disconnection Complete */
        else if (ev_code == 0x05 && param_len >= 4) {
            uint16_t handle = (uint16_t)(param[1] | (param[2] << 8));
            log_line("bt_host: device disconnected (handle 0x%04x)", handle);
            int disconnected_slot = -1;
            pthread_mutex_lock(&g_host.lock);
            for (int i = 0; i < MAX_SLOTS; i++) {
                if (g_host.devices[i].acl_handle == handle) {
                    if (g_host.devices[i].slot >= 0) {
                        disconnected_slot = g_host.devices[i].slot;
                    }
                    g_host.devices[i].connected = 0;
                    g_host.devices[i].slot = -1;
                    break;
                }
            }
            pthread_mutex_unlock(&g_host.lock);
            if (disconnected_slot >= 0 && disconnected_slot < MAX_SLOTS) {
                vpad_remove(disconnected_slot);
            }
        }
    }

    /* Process ACL input data */
    uint8_t acl[HCI_PKT_MAX];
    while ((len = bt_hci_recv_acl(acl, sizeof(acl))) > 0) {
        bt_hid_input_t input;
        int device_index = -1;
        int target_slot = -1;
        uint16_t vid = 0;
        uint16_t pid = 0;
        int found;

        if (!bt_acl_parse_hid_input(acl, (size_t)len, &input)) continue;

        pthread_mutex_lock(&g_host.lock);
        found = bt_find_device_by_acl_handle(g_host.devices, MAX_SLOTS,
                                             input.handle, &device_index,
                                             &target_slot);
        if (found) {
            /* The device-table index and virtual pad slot are independent. */
            vid = g_host.devices[device_index].vid;
            pid = g_host.devices[device_index].pid;
        }
        pthread_mutex_unlock(&g_host.lock);

        if (found) {
            pad_state_t st;
            if (profiles_parse_report(vid, pid, input.report,
                                      input.report_len, &st)) {
                st.conn_type = CONN_BLUETOOTH_CLASSIC;
                vpad_update(target_slot, &st);
            }
        }
    }
}

void bt_host_cleanup(void)
{
    bt_hci_close();
    g_host.pairing_active = 0;
}
