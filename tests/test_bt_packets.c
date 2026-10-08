#include "../src/bt_packets.h"
#include "../src/bt_hci_usb.h"
#include "../src/pad_types.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static size_t make_packet(uint8_t *packet, uint16_t handle,
                          const uint8_t *report, size_t report_len)
{
    size_t l2cap_len = report_len + 1u;
    size_t acl_len = l2cap_len + 4u;
    packet[0] = (uint8_t)(handle & 0xffu);
    packet[1] = (uint8_t)(((handle >> 8) & 0x0fu) | 0x20u);
    packet[2] = (uint8_t)(acl_len & 0xffu);
    packet[3] = (uint8_t)(acl_len >> 8);
    packet[4] = (uint8_t)(l2cap_len & 0xffu);
    packet[5] = (uint8_t)(l2cap_len >> 8);
    packet[6] = (uint8_t)(BT_ACL_HID_CID & 0xffu);
    packet[7] = (uint8_t)(BT_ACL_HID_CID >> 8);
    packet[8] = 0xa1u;
    if (report_len) memcpy(packet + 9, report, report_len);
    return acl_len + 4u;
}

static void expect_invalid(const uint8_t *packet, size_t length)
{
    bt_hid_input_t input;
    memset(&input, 0, sizeof(input));
    assert(!bt_acl_parse_hid_input(packet, length, &input));
}

static void test_malformed_packets(void)
{
    uint8_t packet[HCI_PKT_MAX];
    uint8_t report[BT_HID_REPORT_MAX + 1u];
    size_t length;

    expect_invalid(NULL, 0);
    memset(packet, 0, sizeof(packet));
    expect_invalid(packet, 0);
    expect_invalid(packet, 3);

    /* Eight bytes contain the ACL and L2CAP headers, but no HID header. */
    packet[0] = 0x42; packet[1] = 0x20;
    packet[2] = 4; packet[3] = 0;
    packet[4] = 0; packet[5] = 0;
    packet[6] = 0x13; packet[7] = 0;
    expect_invalid(packet, 8);

    memset(report, 0x5a, sizeof(report));
    length = make_packet(packet, 0x42, report, 2);
    expect_invalid(packet, 8); /* complete L2CAP header, truncated before HID */
    expect_invalid(packet, length - 1u); /* truncated advertised ACL payload */

    packet[2] = (uint8_t)(packet[2] + 4u); /* ACL length exceeds received data */
    expect_invalid(packet, length);
    length = make_packet(packet, 0x42, report, 2);
    packet[4] = (uint8_t)(packet[4] + 3u); /* L2CAP length exceeds ACL data */
    expect_invalid(packet, length);

    length = make_packet(packet, 0x42, report, 2);
    packet[6] = 0x01; /* unsupported L2CAP channel */
    expect_invalid(packet, length);
    length = make_packet(packet, 0, report, 2);
    expect_invalid(packet, length);
    length = make_packet(packet, 0x0fff, report, 2);
    expect_invalid(packet, length);

    length = make_packet(packet, 0x42, report, 2);
    packet[8] = 0x90; /* not HID DATA input */
    expect_invalid(packet, length);
    length = make_packet(packet, 0x42, report, 0); /* HID header only */
    expect_invalid(packet, length);

    length = make_packet(packet, 0x42, report, sizeof(report));
    expect_invalid(packet, length); /* beyond supported HID report bound */
}

static void test_valid_packet_and_device_mapping(void)
{
    uint8_t packet[64];
    uint8_t report[] = {0x31, 0x00, 0x80, 0x80};
    bt_hid_input_t input;
    bt_device_t devices[MAX_SLOTS];
    int index = -1;
    int slot = -1;
    size_t length = make_packet(packet, 0x123, report, sizeof(report));

    assert(bt_acl_parse_hid_input(packet, length, &input));
    assert(input.handle == 0x123);
    assert(input.channel_id == BT_ACL_HID_CID);
    assert(input.report_len == sizeof(report));
    assert(memcmp(input.report, report, sizeof(report)) == 0);

    memset(devices, 0, sizeof(devices));
    devices[0].connected = 1;
    devices[0].acl_handle = 0x123;
    devices[0].slot = 2;
    devices[0].vid = 0x054c;
    devices[0].pid = 0x0ce6;
    devices[2].connected = 1;
    devices[2].acl_handle = 0x456;
    devices[2].slot = 0;

    assert(bt_find_device_by_acl_handle(devices, MAX_SLOTS, input.handle,
                                        &index, &slot));
    assert(index == 0);
    assert(slot == 2);
    assert(devices[index].vid == 0x054c);
    assert(!bt_find_device_by_acl_handle(devices, MAX_SLOTS, 0x777,
                                         &index, &slot));
}

int main(void)
{
    test_malformed_packets();
    test_valid_packet_and_device_mapping();
    puts("Bluetooth ACL/L2CAP regression tests passed.");
    return 0;
}
