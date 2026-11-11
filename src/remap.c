#include "remap.h"
#include "bt_host.h"        /* BT_MAP_DIR */
#include "log.h"

#include <stdio.h>
#include <string.h>

const char *const remap_names[REMAP_N] = {
    "cross", "circle", "square", "triangle", "l1", "r1", "l2", "r2", "l3", "r3",
    "create", "options", "ps", "touchpad", "up", "down", "left", "right"
};
const uint32_t remap_bits[REMAP_N] = {
    PAD_BTN_CROSS, PAD_BTN_CIRCLE, PAD_BTN_SQUARE, PAD_BTN_TRIANGLE, PAD_BTN_L1, PAD_BTN_R1,
    PAD_BTN_L2, PAD_BTN_R2, PAD_BTN_L3, PAD_BTN_R3, PAD_BTN_CREATE, PAD_BTN_OPTIONS,
    PAD_BTN_PS, PAD_BTN_TOUCHPAD, PAD_DPAD_UP, PAD_DPAD_DOWN, PAD_DPAD_LEFT, PAD_DPAD_RIGHT
};
enum { I_L2 = 6, I_R2 = 7 };

static int name_index(const char *s)
{
    for (int i = 0; i < REMAP_N; i++)
        if (strcmp(s, remap_names[i]) == 0) return i;
    return -1;
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
    return s;
}

int remap_load(const char *id, uint32_t map[REMAP_N])
{
    char path[160], line[128];
    int n = 0;

    for (int i = 0; i < REMAP_N; i++) map[i] = remap_bits[i];
    if (!id || !*id) return 0;
    snprintf(path, sizeof path, BT_MAP_DIR "/%s.map", id);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(line), *v = trim(eq + 1);
        if (strncmp(k, "remap_", 6) != 0) continue;
        int from = name_index(k + 6);
        if (from < 0) continue;
        if (strcmp(v, "none") == 0) { map[from] = 0; n++; continue; }
        int to = name_index(v);
        if (to < 0) continue;
        map[from] = remap_bits[to];
        if (to != from) n++;
    }
    fclose(f);
    if (n) log_line("remap %s: %d button override(s)", id, n);
    return n;
}

void remap_apply(const uint32_t map[REMAP_N], pad_state_t *st)
{
    uint32_t known = 0, in = st->buttons, out;
    int l2 = 0, r2 = 0;

    for (int i = 0; i < REMAP_N; i++) known |= remap_bits[i];
    out = in & ~known;
    for (int i = 0; i < REMAP_N; i++) {
        if (!(in & remap_bits[i])) continue;
        out |= map[i];
        /* A digital button sent to a trigger presses it fully. */
        if (i != I_L2 && i != I_R2) {
            if (map[i] & PAD_BTN_L2) l2 = 255;
            if (map[i] & PAD_BTN_R2) r2 = 255;
        }
    }
    /* The analog triggers follow where L2/R2 were sent. */
    if (map[I_L2] & PAD_BTN_L2 && st->l2 > l2) l2 = st->l2;
    if (map[I_R2] & PAD_BTN_L2 && st->r2 > l2) l2 = st->r2;
    if (map[I_L2] & PAD_BTN_R2 && st->l2 > r2) r2 = st->l2;
    if (map[I_R2] & PAD_BTN_R2 && st->r2 > r2) r2 = st->r2;
    /* A trigger sent to a button presses it past the usual threshold. */
    if (st->l2 >= PAD_TRIGGER_DIGITAL_THRESHOLD) out |= map[I_L2];
    if (st->r2 >= PAD_TRIGGER_DIGITAL_THRESHOLD) out |= map[I_R2];
    if (l2 >= PAD_TRIGGER_DIGITAL_THRESHOLD) out |= PAD_BTN_L2; else out &= ~PAD_BTN_L2;
    if (r2 >= PAD_TRIGGER_DIGITAL_THRESHOLD) out |= PAD_BTN_R2; else out &= ~PAD_BTN_R2;
    /* ...unless a digital source asked for it outright. */
    for (int i = 0; i < REMAP_N; i++) {
        if (i == I_L2 || i == I_R2 || !(in & remap_bits[i])) continue;
        out |= map[i];
    }
    st->buttons = out;
    st->l2 = (uint8_t)l2;
    st->r2 = (uint8_t)r2;
}
