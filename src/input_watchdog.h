#ifndef OMNIPAD_INPUT_WATCHDOG_H
#define OMNIPAD_INPUT_WATCHDOG_H

#include "pad_types.h"

/* 500ms is suitable for standard streaming HID devices; it is too short
 * for event-based USB receiver firmware, where no new packet may be sent
 * until the *next* control change. A longer bounded watchdog is a pragmatic
 * diagnostic workaround, NOT radio link health detection. */
#define OMNI_DEFAULT_STALE_TIMEOUT_MS 500L
#define OMNI_EVENT_STALE_TIMEOUT_MS 20000L

static inline int omni_state_has_active_input(const pad_state_t *st)
{
    return st && (st->buttons || st->l2 || st->r2 ||
                  st->lx != 128 || st->ly != 128 ||
                  st->rx != 128 || st->ry != 128);
}

#endif
