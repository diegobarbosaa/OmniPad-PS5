#ifndef BT_PACKETS_H
#define BT_PACKETS_H

#include <stddef.h>
#include <stdint.h>

#define BT_ACL_HID_CID 0x0013u
#define BT_HID_REPORT_MAX 128u

typedef struct {
    uint8_t mac[6];
    char name[64];
    uint16_t vid;
    uint16_t pid;
    uint16_t acl_handle;
    int slot;
    int connected;
    long last_seen;
} bt_device_t;

typedef struct {
    uint16_t handle;
    uint16_t channel_id;
    const uint8_t *report;
    size_t report_len;
} bt_hid_input_t;

/* Parse one complete HCI ACL packet (the packet type byte is not included).
 * Returns 1 for a valid HID input report and 0 for any other/malformed packet.
 */
int bt_acl_parse_hid_input(const uint8_t *packet, size_t packet_len,
                           bt_hid_input_t *input);

/* Return the device-array index and its separate virtual slot for a handle. */
int bt_find_device_by_acl_handle(const bt_device_t *devices, size_t count,
                                 uint16_t handle, int *device_index,
                                 int *virtual_slot);

#endif
