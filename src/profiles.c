#include "profiles.h"
#include "util.h"

#include <string.h>

int profiles_parse_report(uint16_t vid, uint16_t pid, const uint8_t *data, size_t len, pad_state_t *st)
{
    if (!data || len == 0 || !st) return 0;
    (void)pid;
    pad_state_neutral(st);

    /* ---- DualShock 4 over Bluetooth (Report ID 0x11, len >= 78) ---- */
    if (data[0] == 0x11 && len >= 78) {
        /* Byte 1 is 0xC0 or flags, sticks start at offset 3 */
        st->lx = data[3];
        st->ly = data[4];
        st->rx = data[5];
        st->ry = data[6];

        uint8_t dpad = data[7] & 0x0F;
        switch (dpad) {
        case 0: st->buttons |= PAD_DPAD_UP; break;
        case 1: st->buttons |= PAD_DPAD_UP | PAD_DPAD_RIGHT; break;
        case 2: st->buttons |= PAD_DPAD_RIGHT; break;
        case 3: st->buttons |= PAD_DPAD_DOWN | PAD_DPAD_RIGHT; break;
        case 4: st->buttons |= PAD_DPAD_DOWN; break;
        case 5: st->buttons |= PAD_DPAD_DOWN | PAD_DPAD_LEFT; break;
        case 6: st->buttons |= PAD_DPAD_LEFT; break;
        case 7: st->buttons |= PAD_DPAD_UP | PAD_DPAD_LEFT; break;
        default: break;
        }

        uint8_t b1 = data[7];
        if (b1 & 0x10) st->buttons |= PAD_BTN_SQUARE;
        if (b1 & 0x20) st->buttons |= PAD_BTN_CROSS;
        if (b1 & 0x40) st->buttons |= PAD_BTN_CIRCLE;
        if (b1 & 0x80) st->buttons |= PAD_BTN_TRIANGLE;

        uint8_t b2 = data[8];
        if (b2 & 0x01) st->buttons |= PAD_BTN_L1;
        if (b2 & 0x02) st->buttons |= PAD_BTN_R1;
        if (b2 & 0x04) st->buttons |= PAD_BTN_L2;
        if (b2 & 0x08) st->buttons |= PAD_BTN_R2;
        if (b2 & 0x10) st->buttons |= PAD_BTN_SHARE;
        if (b2 & 0x20) st->buttons |= PAD_BTN_OPTIONS;
        if (b2 & 0x40) st->buttons |= PAD_BTN_L3;
        if (b2 & 0x80) st->buttons |= PAD_BTN_R3;

        uint8_t b3 = data[9];
        if (b3 & 0x01) st->buttons |= PAD_BTN_PS;
        if (b3 & 0x02) st->buttons |= PAD_BTN_TOUCHPAD;

        st->l2 = data[10];
        st->r2 = data[11];

        /* Battery level (byte 32) */
        uint8_t bat = data[32] & 0x0F;
        st->battery_level = (bat <= 10) ? bat * 10 : 100;
        st->battery_charging = (data[32] & 0x10) ? 1 : 0;
        return 1;
    }

    /* ---- DualSense over Bluetooth (Report ID 0x31, len >= 78) ---- */
    if (data[0] == 0x31 && len >= 78) {
        st->lx = data[2];
        st->ly = data[3];
        st->rx = data[4];
        st->ry = data[5];
        st->l2 = data[6];
        st->r2 = data[7];

        uint8_t dpad = data[9] & 0x0F;
        switch (dpad) {
        case 0: st->buttons |= PAD_DPAD_UP; break;
        case 1: st->buttons |= PAD_DPAD_UP | PAD_DPAD_RIGHT; break;
        case 2: st->buttons |= PAD_DPAD_RIGHT; break;
        case 3: st->buttons |= PAD_DPAD_DOWN | PAD_DPAD_RIGHT; break;
        case 4: st->buttons |= PAD_DPAD_DOWN; break;
        case 5: st->buttons |= PAD_DPAD_DOWN | PAD_DPAD_LEFT; break;
        case 6: st->buttons |= PAD_DPAD_LEFT; break;
        case 7: st->buttons |= PAD_DPAD_UP | PAD_DPAD_LEFT; break;
        default: break;
        }

        uint8_t b1 = data[9];
        if (b1 & 0x10) st->buttons |= PAD_BTN_SQUARE;
        if (b1 & 0x20) st->buttons |= PAD_BTN_CROSS;
        if (b1 & 0x40) st->buttons |= PAD_BTN_CIRCLE;
        if (b1 & 0x80) st->buttons |= PAD_BTN_TRIANGLE;

        uint8_t b2 = data[10];
        if (b2 & 0x01) st->buttons |= PAD_BTN_L1;
        if (b2 & 0x02) st->buttons |= PAD_BTN_R1;
        if (b2 & 0x04) st->buttons |= PAD_BTN_L2;
        if (b2 & 0x08) st->buttons |= PAD_BTN_R2;
        if (b2 & 0x10) st->buttons |= PAD_BTN_SHARE;
        if (b2 & 0x20) st->buttons |= PAD_BTN_OPTIONS;
        if (b2 & 0x40) st->buttons |= PAD_BTN_L3;
        if (b2 & 0x80) st->buttons |= PAD_BTN_R3;

        uint8_t b3 = data[11];
        if (b3 & 0x01) st->buttons |= PAD_BTN_PS;
        if (b3 & 0x02) st->buttons |= PAD_BTN_TOUCHPAD;
        if (b3 & 0x04) st->buttons |= PAD_BTN_MUTE;

        uint8_t bat = data[54] & 0x0F;
        st->battery_level = (bat <= 10) ? bat * 10 : 100;
        st->battery_charging = (data[54] & 0x10) ? 1 : 0;
        return 1;
    }

    /* ---- Xbox Wireless Controller / Machenike BLE (Report ID 0x01, len >= 16) ---- */
    if (len >= 16 && (data[0] == 0x01 || vid == 0x045e || vid == 0x2563 || vid == 0x2f24 || vid == 0x20bc || vid == 0x3537)) {
        int off = (data[0] == 0x01) ? 1 : 0;
        uint16_t raw_lx = (uint16_t)(data[off + 0] | (data[off + 1] << 8));
        uint16_t raw_ly = (uint16_t)(data[off + 2] | (data[off + 3] << 8));
        uint16_t raw_rx = (uint16_t)(data[off + 4] | (data[off + 5] << 8));
        uint16_t raw_ry = (uint16_t)(data[off + 6] | (data[off + 7] << 8));

        st->lx = (uint8_t)(raw_lx >> 8);
        st->ly = (uint8_t)(255 - (raw_ly >> 8));
        st->rx = (uint8_t)(raw_rx >> 8);
        st->ry = (uint8_t)(255 - (raw_ry >> 8));

        uint16_t raw_lt = (uint16_t)(data[off + 8] | (data[off + 9] << 8));
        uint16_t raw_rt = (uint16_t)(data[off + 10] | (data[off + 11] << 8));
        st->l2 = (uint8_t)(raw_lt >> 2);
        st->r2 = (uint8_t)(raw_rt >> 2);
        if (st->l2 > 30) st->buttons |= PAD_BTN_L2;
        if (st->r2 > 30) st->buttons |= PAD_BTN_R2;

        uint16_t btns = (uint16_t)(data[off + 14] | (data[off + 15] << 8));
        if (btns & 0x0001) st->buttons |= PAD_BTN_CROSS;    /* A */
        if (btns & 0x0002) st->buttons |= PAD_BTN_CIRCLE;   /* B */
        if (btns & 0x0008) st->buttons |= PAD_BTN_SQUARE;   /* X */
        if (btns & 0x0010) st->buttons |= PAD_BTN_TRIANGLE; /* Y */
        if (btns & 0x0040) st->buttons |= PAD_BTN_L1;
        if (btns & 0x0080) st->buttons |= PAD_BTN_R1;
        if (btns & 0x0100) st->buttons |= PAD_BTN_PS;       /* Xbox Guide / Home */
        if (btns & 0x0400) st->buttons |= PAD_BTN_OPTIONS;  /* Menu / Start */
        if (btns & 0x0800) st->buttons |= PAD_BTN_SHARE;    /* View / Back */
        if (btns & 0x2000) st->buttons |= PAD_BTN_L3;
        if (btns & 0x4000) st->buttons |= PAD_BTN_R3;

        uint8_t dpad = data[off + 12];
        if (dpad >= 1 && dpad <= 8) {
            switch (dpad) {
            case 1: st->buttons |= PAD_DPAD_UP; break;
            case 2: st->buttons |= PAD_DPAD_UP | PAD_DPAD_RIGHT; break;
            case 3: st->buttons |= PAD_DPAD_RIGHT; break;
            case 4: st->buttons |= PAD_DPAD_DOWN | PAD_DPAD_RIGHT; break;
            case 5: st->buttons |= PAD_DPAD_DOWN; break;
            case 6: st->buttons |= PAD_DPAD_DOWN | PAD_DPAD_LEFT; break;
            case 7: st->buttons |= PAD_DPAD_LEFT; break;
            case 8: st->buttons |= PAD_DPAD_UP | PAD_DPAD_LEFT; break;
            }
        }
        return 1;
    }

    /* ---- Generic fallback ---- */
    if (len >= 6) {
        st->lx = data[0];
        st->ly = data[1];
        st->rx = data[2];
        st->ry = data[3];
        st->buttons = (uint32_t)(data[4] | (data[5] << 8));
        return 1;
    }

    return 0;
}
