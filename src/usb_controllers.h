#ifndef USB_CONTROLLERS_H
#define USB_CONTROLLERS_H

#include "pad_types.h"
#include <stdint.h>
#include <stddef.h>

typedef enum {
    CTRL_UNKNOWN = 0,
    CTRL_NINTENDO_SWITCH_PRO,
    CTRL_SONY_DS4,
    CTRL_SONY_DS3,
    CTRL_XBOX_XINPUT,
    CTRL_8BITDO_ADAPTER,
    CTRL_GENERIC_HID
} usb_controller_type_t;

usb_controller_type_t usb_identify_controller(uint16_t vid, uint16_t pid, const char **name_out);
int usb_init_controller_handshake(int fd, usb_controller_type_t type);
int usb_parse_input_report(usb_controller_type_t type, const uint8_t *data, size_t len, pad_state_t *st);
int usb_parse_gip_report(const uint8_t *data, size_t len, pad_state_t *st, int *guide_down);
uint8_t axis16_to_u8(int16_t val, int invert_y);

#endif /* USB_CONTROLLERS_H */
