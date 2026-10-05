#ifndef BT_HCI_USB_H
#define BT_HCI_USB_H

#include <stdint.h>
#include <stddef.h>

#define HCI_PKT_MAX 1024

typedef struct {
    uint8_t ep_events;
    uint8_t ep_acl_in;
    uint8_t ep_acl_out;
    uint16_t mps_events;
    uint16_t mps_acl_in;
    uint16_t mps_acl_out;
    int iface;
    char dev_node[32];
} bt_hci_device_info_t;

int bt_hci_init(void);
void bt_hci_poll(void);
int bt_hci_send_cmd(uint16_t ocf, uint8_t ogf, const void *param, uint8_t param_len);
int bt_hci_send_acl(uint16_t handle, uint8_t pb, const void *data, uint16_t len);
int bt_hci_recv_event(uint8_t *out, int max_len);
int bt_hci_recv_acl(uint8_t *out, int max_len);
int bt_hci_is_open(void);
void bt_hci_close(void);
const bt_hci_device_info_t *bt_hci_get_info(void);

#endif /* BT_HCI_USB_H */
