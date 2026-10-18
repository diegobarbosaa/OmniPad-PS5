#ifndef USB_HOTPLUG_H
#define USB_HOTPLUG_H

int usb_hotplug_init(void);
void usb_hotplug_poll(long now);
void usb_hotplug_cleanup(void);

#include <stdint.h>

/* OmniPad: identity of the USB pad in a vpad slot. Returns 0 if none. */
int  usb_hotplug_slot_ids(int slot, uint16_t *vid, uint16_t *pid);
/* 1 if any USB pad is claimed. */
int  usb_hotplug_any(void);
/* Game rumble for the USB pad in `slot` (0..255 each). The pad's reader
 * thread sends it when it changes, for pads whose rumble format is known
 * (Xbox 360 / XInput incl. 8BitDo, Xbox One GIP, DualShock 4, Switch Pro). The weak
 * motor is dropped if that pad's settings turn the small motor off. */
void usb_hotplug_set_rumble(int slot, uint8_t strong, uint8_t weak);
/* Re-read every claimed pad's settings file (after the page saves one). */
void usb_hotplug_reload_configs(void);

#endif /* USB_HOTPLUG_H */
