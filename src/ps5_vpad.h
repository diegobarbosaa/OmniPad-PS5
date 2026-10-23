#ifndef PS5_VPAD_H
#define PS5_VPAD_H

#include "pad_types.h"
#include <stdint.h>

typedef enum {
    VP_FREE = 0,
    VP_QUEUED,
    VP_PENDING,
    VP_READY,
    VP_FAILED
} vpad_status_t;

typedef struct {
    vpad_status_t   status;
    int32_t         handle;
    int32_t         alt_handle;
    uint64_t        device_id;
    int32_t         user_id;
    pad_conn_type_t conn_type;
    char            name[64];
    uint8_t         battery_level;
    uint8_t         battery_charging;
    uint32_t        packets_injected;
    long            connected_time;
    long            last_update_time;
} vpad_slot_info_t;

int vpad_init(void);
void vpad_poll(long now);
int vpad_add(int slot, pad_conn_type_t type, const char *name);
void vpad_remove(int slot);
int vpad_is_live(int slot);
int vpad_slot_is_free(int slot);
void vpad_update(int slot, const pad_state_t *st);
/* Button overrides for a slot (remap.h); NULL or identity turns them off.
 * Set by the BT and USB code whenever a pad takes the slot. */
void vpad_set_remap(int slot, const uint32_t *map);
void vpad_get_slot_info(int slot, vpad_slot_info_t *out);
void vpad_press_ps_button(int slot);
int vpad_rebind_user(int slot, int32_t user_id);
void vpad_cleanup_all(void);

/* Device-type knob: 3 = DualSense (default), 2 = DualShock 4 ABI. Both
 * usually come back as kernel subType:22 (DualSense) on current firmware;
 * flipping to 2 is an experiment to see if the game's rumble routing
 * differs (DS4 only uses the simple-rumble path). */
int  vpad_get_device_type(void);
void vpad_set_device_type(int type);

#endif /* PS5_VPAD_H */
