#ifndef BT_HOST_H
#define BT_HOST_H

#include "pad_types.h"
#include <stdint.h>

int bt_host_init(void);
void bt_host_poll(long now);
void bt_host_start_pairing(int duration_sec);
int bt_host_is_pairing(void);
int bt_host_get_pairing_seconds_left(void);
void bt_host_disconnect_device(int slot);
void bt_host_cleanup(void);

#endif /* BT_HOST_H */
