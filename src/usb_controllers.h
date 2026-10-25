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
    CTRL_GENERIC_HID,
    CTRL_THRUSTMASTER_T598
} usb_controller_type_t;

/* T598 steering sensitivity in percent. 100 = full wheel lock gives full stick.
 * 300 = full stick is reached at one third of the wheel's rotation. */
#ifndef T598_STEER_GAIN_PCT
#define T598_STEER_GAIN_PCT 100
#endif

/* Games ignore small stick movements near center. As soon as the wheel leaves
 * center, the stick output jumps to this percent so small turns still steer. */
#ifndef T598_STEER_ANTIDEADZONE_PCT
#define T598_STEER_ANTIDEADZONE_PCT 40 /* tuned on hardware in Star Wars: Galactic Racer */
#endif

/* Wheel movement (raw units out of 65536) treated as "centered", to avoid jitter.
 * 96 is about 1.5 degrees at 1080 degrees of rotation (the wheel rests ~84 off center). */
#ifndef T598_STEER_CENTER_DZ
#define T598_STEER_CENTER_DZ 96
#endif

usb_controller_type_t usb_identify_controller(uint16_t vid, uint16_t pid, const char **name_out);
int usb_init_controller_handshake(int fd, usb_controller_type_t type);
int usb_parse_input_report(usb_controller_type_t type, const uint8_t *data, size_t len, pad_state_t *st);
uint8_t axis16_to_u8(int16_t val, int invert_y);

#endif /* USB_CONTROLLERS_H */
