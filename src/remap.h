/* Per-controller button overrides on top of the auto-detected layout.
 *
 * Every pad, Bluetooth or USB, is first mapped to DualSense buttons by its
 * profile/parser (the auto-detected layout). A controller's settings file,
 * <map dir>/<id>.map with id = BT address (AABBCCDDEEFF) or USB_VVVV_PPPP,
 * may then override single buttons:  remap_<from> = <to>  (to may be "none").
 * Buttons without a line keep the auto-detected mapping. */
#ifndef REMAP_H
#define REMAP_H

#include "pad_types.h"

#define REMAP_N 18

extern const char *const remap_names[REMAP_N];
extern const uint32_t    remap_bits[REMAP_N];

/* Fills map[i] with the target mask for button i (identity by default).
 * Returns the number of overrides found (0: identity). */
int  remap_load(const char *id, uint32_t map[REMAP_N]);
/* Rewrites st->buttons (and the analog triggers that follow L2/R2). */
void remap_apply(const uint32_t map[REMAP_N], pad_state_t *st);

#endif
