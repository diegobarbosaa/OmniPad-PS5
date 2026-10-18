#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/ps_chord_hold.h"
#include "../src/input_watchdog.h"

static pad_state_t chord(void) {
    pad_state_t s;
    pad_state_neutral(&s);
    s.buttons = PAD_BTN_SHARE | PAD_BTN_OPTIONS | PAD_BTN_R2;
    s.r2 = 235; s.lx = 28;
    return s;
}
int main(void) {
    omni_ps_chord_hold_t h = {0}, other = {0};
    pad_state_t raw = chord(), out;
    const long timeout = OMNI_EVENT_STALE_TIMEOUT_MS;
    assert(timeout == 20000);
    omni_ps_hold_report(&h, &raw, &out, 1000, timeout);
    assert(!(out.buttons & (PAD_BTN_PS | PAD_BTN_SHARE | PAD_BTN_OPTIONS)));
    assert(out.lx == 28 && out.r2 == 235 && (out.buttons & PAD_BTN_R2));
    assert(!omni_ps_hold_tick(&h, &out, 1599, timeout));
    assert(omni_ps_hold_tick(&h, &out, 1600, timeout));
    assert(out.buttons & PAD_BTN_PS);
    assert(!omni_ps_hold_tick(&h, &out, 1670, timeout));
    assert(h.held && h.last_update_ms == 1600);
    assert(!omni_ps_hold_tick(&h, &out, 21600, timeout));
    assert(h.held && h.last_update_ms == 1600);
    assert(!omni_ps_hold_tick(&h, &out, 21601, timeout));
    assert(h.blocked && !h.held);
    /* Even fresh held reports cannot restart a timed-out chord. */
    omni_ps_hold_report(&h, &raw, &out, 22000, timeout);
    assert(h.blocked && !(out.buttons & PAD_BTN_PS));
    assert(!omni_ps_hold_tick(&h, &out, 23000, timeout));
    raw.buttons &= ~PAD_BTN_OPTIONS;
    omni_ps_hold_report(&h, &raw, &out, 23001, timeout);
    assert(!h.blocked && !h.held && (out.buttons & PAD_BTN_SHARE));
    raw = chord();
    omni_ps_hold_report(&h, &raw, &out, 24000, timeout);
    raw.lx = 40; raw.r2 = 255;
    omni_ps_hold_report(&h, &raw, &out, 24600, timeout);
    assert(h.held && (out.buttons & PAD_BTN_PS) && out.lx == 40 && out.r2 == 255);
    for (int i = 1; i <= 10; i++) {
        raw.rx = (uint8_t)i;
        omni_ps_hold_report(&h, &raw, &out, 24600 + i * 3000, timeout);
        assert(h.held && out.rx == i && (out.buttons & PAD_BTN_PS));
    }
    /* Releasing either chord button releases only synthetic PS. */
    for (int release = 0; release < 2; release++) {
        h = (omni_ps_chord_hold_t){0};
        raw = chord();
        omni_ps_hold_report(&h, &raw, &out, 1000, timeout);
        assert(omni_ps_hold_tick(&h, &out, 1600, timeout));
        raw.buttons &= ~(release ? PAD_BTN_SHARE : PAD_BTN_OPTIONS);
        omni_ps_hold_report(&h, &raw, &out, 1601, timeout);
        assert(!h.held && !(out.buttons & PAD_BTN_PS));
    }
    h = (omni_ps_chord_hold_t){0}; raw = chord();
    omni_ps_hold_report(&h, &raw, &out, 1000, timeout);
    raw.buttons &= ~PAD_BTN_OPTIONS;
    omni_ps_hold_report(&h, &raw, &out, 1599, timeout);
    assert(!omni_ps_hold_tick(&h, &out, 1600, timeout));
    raw.buttons |= PAD_BTN_PS;
    omni_ps_hold_report(&h, &raw, &out, 1700, timeout);
    assert(out.buttons & PAD_BTN_PS); /* Never clear physical Home. */
    assert(!other.valid && !other.held); /* Per-reader isolation. */
    h = (omni_ps_chord_hold_t){0}; raw = chord();
    omni_ps_hold_report(&h, &raw, &out, 1000, timeout);
    assert(!omni_ps_hold_tick(&h, &out, 22000, timeout)); /* Expired wait. */
    assert(h.blocked);
    h = (omni_ps_chord_hold_t){0}; /* Removal/recycle discards state. */
    assert(!omni_ps_hold_tick(&h, &out, 23000, timeout));
    puts("G7 PS hold timing, release, watchdog and input preservation passed.");
    return 0;
}
