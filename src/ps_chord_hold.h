#ifndef OMNIPAD_PS_CHORD_HOLD_H
#define OMNIPAD_PS_CHORD_HOLD_H

#include "pad_types.h"

/* Reader-owned G7 hold state. No model identification or USB operations here.
 * Cached input must never refresh last_update_ms on an ordinary timer tick. */
typedef struct {
    long started_ms;
    long last_update_ms;
    int waiting, held, blocked, valid;
    pad_state_t raw;
} omni_ps_chord_hold_t;

static inline void omni_ps_hold_expire(omni_ps_chord_hold_t *h,
                                      long now, long timeout_ms)
{
    if (h->valid && (now < h->last_update_ms ||
                    now - h->last_update_ms > timeout_ms)) {
        if (h->waiting || h->held) h->blocked = 1;
        h->waiting = h->held = 0;
    }
}

static inline void omni_ps_hold_filter(const omni_ps_chord_hold_t *h,
                                      pad_state_t *out)
{
    *out = h->raw;
    const uint32_t both = PAD_BTN_SHARE | PAD_BTN_OPTIONS;
    if ((out->buttons & both) == both) out->buttons &= ~both;
    if (h->held) out->buttons |= PAD_BTN_PS;
}

static inline void omni_ps_hold_report(omni_ps_chord_hold_t *h,
                                      const pad_state_t *raw, pad_state_t *out,
                                      long now, long timeout_ms)
{
    const uint32_t both = PAD_BTN_SHARE | PAD_BTN_OPTIONS;
    omni_ps_hold_expire(h, now, timeout_ms);
    h->raw = *raw;
    h->valid = 1;
    if ((raw->buttons & both) != both) {
        h->waiting = h->held = h->blocked = 0;
    } else if (!h->blocked) {
        if (!h->waiting && !h->held) {
            h->started_ms = now;
            h->waiting = 1;
        }
        if (h->waiting && now - h->started_ms >= 600) {
            h->waiting = 0;
            h->held = 1;
        }
    }
    h->last_update_ms = now; /* Fresh decoded input only. */
    omni_ps_hold_filter(h, out);
}

/* Emit once when a silent receiver's initial delay expires. The ordinary
 * vpad watchdog releases stale input; no recurring synthetic keepalive. */
static inline int omni_ps_hold_tick(omni_ps_chord_hold_t *h,
                                   pad_state_t *out, long now, long timeout_ms)
{
    omni_ps_hold_expire(h, now, timeout_ms);
    if (!h->waiting || h->blocked || now - h->started_ms < 600) return 0;
    h->waiting = 0;
    h->held = 1;
    /* Cached transition retains the last physical report timestamp. */
    omni_ps_hold_filter(h, out);
    return 1;
}
#endif
