#include "bt_packets.h"

#include "pad_types.h"

int bt_acl_parse_hid_input(const uint8_t *packet, size_t packet_len,
                           bt_hid_input_t *input)
{
    uint16_t handle_flags;
    uint16_t handle;
    uint16_t acl_payload_len;
    uint16_t l2cap_len;
    uint16_t channel_id;
    size_t available;
    size_t report_len;

    if (!packet || !input || packet_len < 4u) return 0;

    handle_flags = (uint16_t)((uint16_t)packet[0] |
                              ((uint16_t)packet[1] << 8));
    handle = (uint16_t)(handle_flags & 0x0fffu);
    /* PB=00 and PB=10 identify the start of an ACL L2CAP packet. */
    if (handle == 0u || handle > 0x0effu ||
        ((handle_flags >> 12) & 0x03u) == 0x01u ||
        ((handle_flags >> 12) & 0x03u) == 0x03u) {
        return 0;
    }

    acl_payload_len = (uint16_t)((uint16_t)packet[2] |
                                 ((uint16_t)packet[3] << 8));
    available = packet_len - 4u;
    if ((size_t)acl_payload_len != available || available < 4u) return 0;

    l2cap_len = (uint16_t)((uint16_t)packet[4] |
                           ((uint16_t)packet[5] << 8));
    channel_id = (uint16_t)((uint16_t)packet[6] |
                            ((uint16_t)packet[7] << 8));
    if (channel_id != BT_ACL_HID_CID ||
        (size_t)l2cap_len != available - 4u || l2cap_len <= 1u) {
        return 0;
    }

    /* One HIDP header byte must be present before the report payload. */
    if ((packet[8] & 0xf0u) != 0xa0u) return 0;
    report_len = (size_t)l2cap_len - 1u;
    if (report_len == 0u || report_len > BT_HID_REPORT_MAX) return 0;

    input->handle = handle;
    input->channel_id = channel_id;
    input->report = packet + 9u;
    input->report_len = report_len;
    return 1;
}

int bt_find_device_by_acl_handle(const bt_device_t *devices, size_t count,
                                 uint16_t handle, int *device_index,
                                 int *virtual_slot)
{
    size_t i;

    if (!devices || !device_index || !virtual_slot || handle == 0u ||
        handle > 0x0effu || count > (size_t)MAX_SLOTS) {
        return 0;
    }

    for (i = 0; i < count; i++) {
        if (devices[i].connected && devices[i].acl_handle == handle &&
            devices[i].slot >= 0 && devices[i].slot < MAX_SLOTS) {
            *device_index = (int)i;
            *virtual_slot = devices[i].slot;
            return 1;
        }
    }
    return 0;
}
