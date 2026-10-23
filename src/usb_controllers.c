#include "usb_controllers.h"
#include "pad_types.h"
#include "log.h"
#include "util.h"
#include "input_watchdog.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#ifdef __PROSPERO__
#include <dev/usb/usb.h>
#include <dev/usb/usb_ioctl.h>
#include <dev/usb/usb_endian.h>
#endif

usb_controller_type_t usb_identify_controller(uint16_t vid, uint16_t pid, const char **name_out)
{
    /* Nintendo Switch controllers */
    if (vid == 0x057e) {
        if (pid == 0x2009) {
            if (name_out) *name_out = "Nintendo Switch Pro (USB)";
            return CTRL_NINTENDO_SWITCH_PRO;
        }
        if (pid == 0x2017 || pid == 0x2019 || pid == 0x201a) {
            if (name_out) *name_out = "Nintendo NSO Controller (USB)";
            return CTRL_NINTENDO_SWITCH_PRO;
        }
    }

    /* Sony DualShock 4 */
    if (vid == 0x054c) {
        if (pid == 0x05c4 || pid == 0x09cc || pid == 0x0ba0) {
            if (name_out) *name_out = "Sony DualShock 4 (USB)";
            return CTRL_SONY_DS4;
        }
        if (pid == 0x0268) {
            if (name_out) *name_out = "Sony DualShock 3 (USB)";
            return CTRL_SONY_DS3;
        }
    }

    /* Xbox / XInput controllers */
    if (vid == 0x045e) {
        if (pid == 0x028e) {
            if (name_out) *name_out = "Xbox 360 / Machenike (XInput)";
            return CTRL_XBOX_XINPUT;
        }
        if (pid == 0x02d1 || pid == 0x02dd ||
            pid == 0x02e3 || pid == 0x02ea || pid == 0x0b00 ||
            pid == 0x0b05 || pid == 0x0b12 || pid == 0x0b13) {
            if (name_out) *name_out = "Xbox Controller (USB/XInput)";
            return CTRL_XBOX_XINPUT;
        }
    }

    /* HID identities/layouts reported by Essyekky32 (Essy), issue #16.
     * Other GameSir IDs retain the pre-existing generic classification. */
    if (vid == 0x3537 && (pid == 0x1022 || pid == 0x1082)) {
        if (pid == 0x1022) {
            if (name_out) *name_out = "GameSir G7 Pro (USB HID)";
            return CTRL_GAMESIR_G7PRO;
        }
        if (name_out) *name_out = "GameSir G7 SE (USB HID)";
        return CTRL_GAMESIR_G7SE;
    }

    /* Machenike, ShanWan & Thunderobot native VIDs (Wired USB-C, Dongle & PC modes) */
    if (vid == 0x2345 || vid == 0x2563 || vid == 0x2f24 || vid == 0x20bc || vid == 0x3537 || vid == 0x0e8f) {
        if (name_out) *name_out = "MACHENIKE G5 Pro / ShanWan Pad";
        return CTRL_XBOX_XINPUT;
    }

    /* ShanWan / Machenike 2.4G wireless dongle receiver */
    if (vid == 0x1a34 && pid == 0xf517) {
        if (name_out) *name_out = "Machenike 2.4G Wireless Dongle";
        return CTRL_XBOX_XINPUT;
    }
    if (vid == 0x1a34) {
        if (name_out) *name_out = "ShanWan Gamepad";
        return CTRL_XBOX_XINPUT;
    }

    /* 8BitDo adapters & controllers */
    if (vid == 0x2dc8) {
        if (pid == 0x310a || pid == 0x310b || pid == 0x2100 || pid == 0x3012) {
            if (name_out) *name_out = "8BitDo Adapter/Pad (USB)";
            return CTRL_8BITDO_ADAPTER;
        }
    }

    /* Mad Catz Street Fighter IV Wireless FightPad (PS3 RF pad), issue #10.
     * 27-byte HID reports; layout captured by the reporter on Windows. */
    if (vid == 0x0738 && pid == 0x8828) {
        if (name_out) *name_out = "Mad Catz SF IV FightPad (USB HID)";
        return CTRL_MADCATZ_SF4_FIGHTPAD;
    }

    /* Generic PC/HID Gamepad known VIDs (DragonRise, Betop, Retro-Bit, Mayflash, etc.) */
    if (vid == 0x0079 || vid == 0x0810 || vid == 0x11ff || vid == 0x12bd ||
        vid == 0x20d6 || vid == 0x24c6 || vid == 0x146b || vid == 0x0738 ||
        vid == 0x1038 || vid == 0x1532) {
        if (name_out) *name_out = "Generic USB HID Gamepad";
        return CTRL_GENERIC_HID;
    }

    /* Isolate non-gamepad devices (USB flash drives, SSDs, keyboards, webcams) */
    if (name_out) *name_out = "Unknown / Non-Gamepad USB Device";
    return CTRL_UNKNOWN;
}

long usb_controller_stale_timeout_ms(usb_controller_type_t type)
{
    return type == CTRL_GAMESIR_G7PRO ? OMNI_EVENT_STALE_TIMEOUT_MS : OMNI_DEFAULT_STALE_TIMEOUT_MS;
}

int usb_init_controller_handshake(int fd, usb_controller_type_t type)
{
    (void)type;
    if (fd < 0) return -1;

#ifdef __PROSPERO__
    struct usb_ctl_request req;

    if (type == CTRL_NINTENDO_SWITCH_PRO) {
        /* The reader owns the IN endpoint. It will perform the USB handshake
         * sequentially, waiting for 0x81 acknowledgements from the controller.
         * Sending from probe drops the replies and leads to disconnects. */
        return 0;
    }
    else if (type == CTRL_SONY_DS3) {
        /* DualShock 3 magic initialization report */
        uint8_t ds3_init[] = { 0x42, 0x03, 0x00, 0x00 };
        memset(&req, 0, sizeof(req));
        req.ucr_request.bmRequestType = 0x21;
        req.ucr_request.bRequest = 0x09;
        USETW(req.ucr_request.wValue, 0x03f4);
        USETW(req.ucr_request.wLength, sizeof(ds3_init));
        req.ucr_data = ds3_init;
        ioctl(fd, USB_DO_REQUEST, &req);
        log_line("usb_ctrl: DualShock 3 magic initialization sent");
        return 0;
    }
    else if (type == CTRL_XBOX_XINPUT) {
        /* Handshake 1: ShanWan / Machenike / Clone wake-up control transfer.
         * Many third-party 2.4G dongles (045e:028e clones) boot into dormant mode
         * and will not emit input until this control transfer is dispatched. */
        uint8_t dummy[32];
        memset(&req, 0, sizeof(req));
        req.ucr_request.bmRequestType = 0xc1; /* Device to host, vendor, interface */
        req.ucr_request.bRequest = 0x01;
        USETW(req.ucr_request.wValue, 0x0100);
        USETW(req.ucr_request.wIndex, 0x0000);
        USETW(req.ucr_request.wLength, 0x0014);
        req.ucr_data = dummy;
        int r1 = ioctl(fd, USB_DO_REQUEST, &req);
        usleep(20000);

        /* Handshake 2: Xbox 360 Player 1 LED initialization */
        uint8_t x360_led[] = { 0x01, 0x03, 0x02 };
        memset(&req, 0, sizeof(req));
        req.ucr_request.bmRequestType = 0x21; /* Host to device, class, interface */
        req.ucr_request.bRequest = 0x09;      /* SET_REPORT */
        USETW(req.ucr_request.wValue, 0x0200);
        USETW(req.ucr_request.wIndex, 0x0000);
        USETW(req.ucr_request.wLength, sizeof(x360_led));
        req.ucr_data = x360_led;
        int r2 = ioctl(fd, USB_DO_REQUEST, &req);
        log_line("usb_ctrl: XInput wake-up sent (r1=%d, r2=%d)", r1, r2);
        return 0;
    }
#endif
    return 0;
}

static void gamesir_parse_gamepad(const uint8_t *p, pad_state_t *st)
{
    uint8_t b0 = p[0], b1 = p[1];
    if (b0 & 0x01) st->buttons |= PAD_BTN_CROSS;
    if (b0 & 0x02) st->buttons |= PAD_BTN_CIRCLE;
    if (b0 & 0x08) st->buttons |= PAD_BTN_SQUARE;
    if (b0 & 0x10) st->buttons |= PAD_BTN_TRIANGLE;
    if (b0 & 0x40) st->buttons |= PAD_BTN_L1;
    if (b0 & 0x80) st->buttons |= PAD_BTN_R1;
    if (b1 & 0x01) st->buttons |= PAD_BTN_L2;
    if (b1 & 0x02) st->buttons |= PAD_BTN_R2;
    if (b1 & 0x04) st->buttons |= PAD_BTN_SHARE;
    if (b1 & 0x08) st->buttons |= PAD_BTN_OPTIONS;
    if (b1 & 0x20) st->buttons |= PAD_BTN_L3;
    if (b1 & 0x40) st->buttons |= PAD_BTN_R3;
    switch (p[2] & 0x0f) {
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
    st->lx = p[3]; st->ly = p[4]; st->rx = p[5]; st->ry = p[6];
    st->l2 = p[7]; st->r2 = p[8];
    if (st->l2 >= PAD_TRIGGER_DIGITAL_THRESHOLD) st->buttons |= PAD_BTN_L2;
    if (st->r2 >= PAD_TRIGGER_DIGITAL_THRESHOLD) st->buttons |= PAD_BTN_R2;
}

/* Nintendo output report 0x80 (SET_REPORT type 2, ID 0x80).
 * Issue #16: 0x0101 used by older implementations is an INPUT report. */
int usb_nintendo_switch_send_cmd(int fd, uint8_t subcmd)
{
#ifdef __PROSPERO__
    if (fd < 0) return -1;
    struct usb_ctl_request req;
    uint8_t frame[2] = { 0x80, subcmd };
    memset(&req, 0, sizeof(req));
    req.ucr_request.bmRequestType = 0x21;
    req.ucr_request.bRequest = 0x09;
    USETW(req.ucr_request.wValue, 0x0280);
    USETW(req.ucr_request.wIndex, 0x0000);
    USETW(req.ucr_request.wLength, sizeof(frame));
    req.ucr_data = frame;
    return ioctl(fd, USB_DO_REQUEST, &req);
#else
    (void)fd; (void)subcmd;
    return -1;
#endif
}

uint8_t axis16_to_u8(int16_t val, int invert_y)
{
    if (val == 0) return 128;
    int v = ((int)val + 32768) >> 8;
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (uint8_t)(invert_y ? (255 - v) : v);
}

int usb_parse_gip_report(const uint8_t *data, size_t len, pad_state_t *st, int *guide_down)
{
    if (!data || len < 4) return 0;
    if (data[0] == 0x20) {
        if (len < 18 || !st) return 0;
        if (!usb_parse_input_report(CTRL_XBOX_XINPUT, data, len, st)) return 0;
        st->buttons &= ~PAD_BTN_PS;
        return 1;
    }
    if (data[0] == 0x07) {
        if (len < 5 || !guide_down) return 0;
        *guide_down = (data[4] & 0x03) ? 1 : 0;
        return 2;
    }
    return 0;
}

int usb_parse_input_report(usb_controller_type_t type, const uint8_t *data, size_t len, pad_state_t *st)
{
    if (!data || len == 0 || !st) return 0;
    pad_state_neutral(st);

    switch (type) {
    case CTRL_GAMESIR_G7PRO:
        if (len < 9) return 0;
        gamesir_parse_gamepad(data, st);
        return 1;
    case CTRL_GAMESIR_G7SE:
        if (len < 10 || data[0] != 0x05) return 0;
        gamesir_parse_gamepad(data + 1, st);
        return 1;

    case CTRL_MADCATZ_SF4_FIGHTPAD: {
        if (len < 3) return 0;
        uint8_t f = data[0], sy = data[1];
        if (f & 0x01) st->buttons |= PAD_BTN_SQUARE;
        if (f & 0x02) st->buttons |= PAD_BTN_CROSS;
        if (f & 0x04) st->buttons |= PAD_BTN_CIRCLE;
        if (f & 0x08) st->buttons |= PAD_BTN_TRIANGLE;
        if (f & 0x10) st->buttons |= PAD_BTN_L1;
        if (f & 0x20) st->buttons |= PAD_BTN_R1;
        if (f & 0x40) { st->buttons |= PAD_BTN_L2; st->l2 = 255; }
        if (f & 0x80) { st->buttons |= PAD_BTN_R2; st->r2 = 255; }
        if (sy & 0x01) st->buttons |= PAD_BTN_SHARE;
        if (sy & 0x02) st->buttons |= PAD_BTN_OPTIONS;
        if (sy & 0x10) st->buttons |= PAD_BTN_PS;
        /* Standard 8-way hat: neutral 0x0F; captured cardinals 0/2/4/6.
         * Diagonals (1/3/5/7) are NOT captured yet: assumed standard hat order. */
        switch (data[2] & 0x0f) {
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
        return 1;
    }

    case CTRL_NINTENDO_SWITCH_PRO: {
        /* Reject 0x81 handshake replies and 0x21 subcommand responses.
         * Those packets are NOT buttons; never generic-HID fallback them. */
        if (len < 12 || data[0] != 0x30) return 0;
        uint8_t b1 = data[3], b2 = data[4], b3 = data[5];
        /* Nintendo order: Y X B A SR SL R ZR */
        if (b1 & 0x01) st->buttons |= PAD_BTN_SQUARE;
        if (b1 & 0x02) st->buttons |= PAD_BTN_TRIANGLE;
        if (b1 & 0x04) st->buttons |= PAD_BTN_CROSS;
        if (b1 & 0x08) st->buttons |= PAD_BTN_CIRCLE;
        if (b1 & 0x40) st->buttons |= PAD_BTN_R1;
        if (b1 & 0x80) { st->buttons |= PAD_BTN_R2; st->r2 = 255; }
        /* Minus, Plus, R stick, L stick, Home, Capture */
        if (b2 & 0x01) st->buttons |= PAD_BTN_SHARE;
        if (b2 & 0x02) st->buttons |= PAD_BTN_OPTIONS;
        if (b2 & 0x04) st->buttons |= PAD_BTN_R3;
        if (b2 & 0x08) st->buttons |= PAD_BTN_L3;
        if (b2 & 0x10) st->buttons |= PAD_BTN_PS;
        if (b2 & 0x20) st->buttons |= PAD_BTN_TOUCHPAD;
        if (b3 & 0x01) st->buttons |= PAD_DPAD_DOWN;
        if (b3 & 0x02) st->buttons |= PAD_DPAD_UP;
        if (b3 & 0x04) st->buttons |= PAD_DPAD_RIGHT;
        if (b3 & 0x08) st->buttons |= PAD_DPAD_LEFT;
        if (b3 & 0x40) st->buttons |= PAD_BTN_L1;
        if (b3 & 0x80) { st->buttons |= PAD_BTN_L2; st->l2 = 255; }
        /* 12-bit packed sticks (centre 0x800). */
        uint16_t lx = (uint16_t)(data[6] | ((uint16_t)(data[7] & 0x0f) << 8));
        uint16_t ly = (uint16_t)((data[7] >> 4) | ((uint16_t)data[8] << 4));
        uint16_t rx = (uint16_t)(data[9] | ((uint16_t)(data[10] & 0x0f) << 8));
        uint16_t ry = (uint16_t)((data[10] >> 4) | ((uint16_t)data[11] << 4));
        /* Preserve upstream b7682ef vertical conversion semantics. */
        uint8_t ly_u8 = (uint8_t)(ly >> 4);
        uint8_t ry_u8 = (uint8_t)(ry >> 4);
        st->lx = (uint8_t)(lx >> 4);
        st->ly = (ly_u8 == 128) ? 128 : (uint8_t)(255 - ly_u8);
        st->rx = (uint8_t)(rx >> 4);
        st->ry = (ry_u8 == 128) ? 128 : (uint8_t)(255 - ry_u8);
        return 1;
    }

    case CTRL_SONY_DS4: {
        /* Standard DS4 USB report: report ID 0x01, sticks at offsets 1..4 */
        int offset = (data[0] == 0x01) ? 1 : 0;
        if (len < (size_t)(offset + 9)) return 0;

        st->lx = data[offset + 0];
        st->ly = data[offset + 1];
        st->rx = data[offset + 2];
        st->ry = data[offset + 3];

        uint8_t dpad = data[offset + 4] & 0x0F;
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

        uint8_t b1 = data[offset + 4];
        if (b1 & 0x10) st->buttons |= PAD_BTN_SQUARE;
        if (b1 & 0x20) st->buttons |= PAD_BTN_CROSS;
        if (b1 & 0x40) st->buttons |= PAD_BTN_CIRCLE;
        if (b1 & 0x80) st->buttons |= PAD_BTN_TRIANGLE;

        uint8_t b2 = data[offset + 5];
        if (b2 & 0x01) st->buttons |= PAD_BTN_L1;
        if (b2 & 0x02) st->buttons |= PAD_BTN_R1;
        if (b2 & 0x04) st->buttons |= PAD_BTN_L2;
        if (b2 & 0x08) st->buttons |= PAD_BTN_R2;
        if (b2 & 0x10) st->buttons |= PAD_BTN_SHARE;
        if (b2 & 0x20) st->buttons |= PAD_BTN_OPTIONS;
        if (b2 & 0x40) st->buttons |= PAD_BTN_L3;
        if (b2 & 0x80) st->buttons |= PAD_BTN_R3;

        uint8_t b3 = data[offset + 6];
        if (b3 & 0x01) st->buttons |= PAD_BTN_PS;
        if (b3 & 0x02) st->buttons |= PAD_BTN_TOUCHPAD;

        st->l2 = data[offset + 7];
        st->r2 = data[offset + 8];
        return 1;
    }

    case CTRL_XBOX_XINPUT:
    case CTRL_8BITDO_ADAPTER: {
        /* 1. Xbox One GIP Controller Input Report */
        if (data[0] == 0x20 && len >= 18) {
            uint16_t btn = (uint16_t)(data[4] | ((uint16_t)data[5] << 8));
            btn &= (uint16_t)~0x0002u; /* Bit 2 is GIP keep-alive flag */

            if (btn & 0x0004) st->buttons |= PAD_BTN_OPTIONS;  /* Menu / Start */
            if (btn & 0x0008) st->buttons |= PAD_BTN_SHARE;    /* View / Back */
            if (btn & 0x0010) st->buttons |= PAD_BTN_CROSS;    /* A -> Cross */
            if (btn & 0x0020) st->buttons |= PAD_BTN_CIRCLE;   /* B -> Circle */
            if (btn & 0x0040) st->buttons |= PAD_BTN_SQUARE;   /* X -> Square */
            if (btn & 0x0080) st->buttons |= PAD_BTN_TRIANGLE; /* Y -> Triangle */
            if (btn & 0x0100) st->buttons |= PAD_DPAD_UP;
            if (btn & 0x0200) st->buttons |= PAD_DPAD_DOWN;
            if (btn & 0x0400) st->buttons |= PAD_DPAD_LEFT;
            if (btn & 0x0800) st->buttons |= PAD_DPAD_RIGHT;
            if (btn & 0x1000) st->buttons |= PAD_BTN_L1;
            if (btn & 0x2000) st->buttons |= PAD_BTN_R1;
            if (btn & 0x4000) st->buttons |= PAD_BTN_L3;
            if (btn & 0x8000) st->buttons |= PAD_BTN_R3;

            /* 16-bit analog triggers (0-1023) */
            uint16_t trig_l = (uint16_t)(data[6] | ((uint16_t)data[7] << 8));
            uint16_t trig_r = (uint16_t)(data[8] | ((uint16_t)data[9] << 8));
            st->l2 = (uint8_t)(trig_l > 1023 ? 255 : (trig_l >> 2));
            st->r2 = (uint8_t)(trig_r > 1023 ? 255 : (trig_r >> 2));
            if (st->l2 >= PAD_TRIGGER_DIGITAL_THRESHOLD) st->buttons |= PAD_BTN_L2;
            if (st->r2 >= PAD_TRIGGER_DIGITAL_THRESHOLD) st->buttons |= PAD_BTN_R2;

            /* 16-bit signed joysticks -> 8-bit unsigned coordinates */
            int16_t sx = (int16_t)(data[10] | ((uint16_t)data[11] << 8));
            int16_t sy = (int16_t)(data[12] | ((uint16_t)data[13] << 8));
            int16_t rx = (int16_t)(data[14] | ((uint16_t)data[15] << 8));
            int16_t ry = (int16_t)(data[16] | ((uint16_t)data[17] << 8));

            st->lx = axis16_to_u8(sx, 0);
            st->ly = axis16_to_u8(sy, 1);
            st->rx = axis16_to_u8(rx, 0);
            st->ry = axis16_to_u8(ry, 1);
            return 1;
        }

        /* 2. Xbox One GIP Guide (PS) Button Packet */
        if (data[0] == 0x07 && len >= 5) {
            if ((len >= 6 && data[4] == 0x5b && (data[5] & 0x01)) || (data[4] & 0x03)) {
                st->buttons |= PAD_BTN_PS;
                return 1;
            }
            return 0;
        }

        /* 3. Standard Wired Xbox 360 / Wireless Receiver / Generic XInput */
        int off = -1;
        if (len >= 14 && data[0] == 0x00) {
            /* Any Xbox 360 packet starting with 0x00 has button data at offset 2.
             * Byte 1 is packet length (0x14 for official, but clones may report 0x00 or variable). */
            off = 2;
        } else if (len >= 18 && (data[1] & 0x01) && (data[0] == 0x00 || (data[0] & 0x08))) {
            off = 4; /* Wireless receiver */
        }

        if (off < 0 || len < (size_t)(off + 12)) {
            /* Fallback to generic HID parser if XInput header did not match */
            return usb_parse_input_report(CTRL_GENERIC_HID, data, len, st);
        }

        uint16_t btn = (uint16_t)(data[off + 0] | ((uint16_t)data[off + 1] << 8));

        if (btn & 0x0001) st->buttons |= PAD_DPAD_UP;
        if (btn & 0x0002) st->buttons |= PAD_DPAD_DOWN;
        if (btn & 0x0004) st->buttons |= PAD_DPAD_LEFT;
        if (btn & 0x0008) st->buttons |= PAD_DPAD_RIGHT;
        if (btn & 0x0010) st->buttons |= PAD_BTN_OPTIONS; /* Start -> Options */
        if (btn & 0x0020) st->buttons |= PAD_BTN_SHARE;   /* Back -> Share */
        if (btn & 0x0040) st->buttons |= PAD_BTN_L3;
        if (btn & 0x0080) st->buttons |= PAD_BTN_R3;
        if (btn & 0x0100) st->buttons |= PAD_BTN_L1;
        if (btn & 0x0200) st->buttons |= PAD_BTN_R1;
        if (btn & 0x0400) st->buttons |= PAD_BTN_PS;       /* Guide -> PS */
        if (btn & 0x1000) st->buttons |= PAD_BTN_CROSS;    /* A -> Cross */
        if (btn & 0x2000) st->buttons |= PAD_BTN_CIRCLE;   /* B -> Circle */
        if (btn & 0x4000) st->buttons |= PAD_BTN_SQUARE;   /* X -> Square */
        if (btn & 0x8000) st->buttons |= PAD_BTN_TRIANGLE; /* Y -> Triangle */

        /* Triggers: present at off+2 and off+3 */
        st->l2 = data[off + 2];
        st->r2 = data[off + 3];
        if (st->l2 >= PAD_TRIGGER_DIGITAL_THRESHOLD) st->buttons |= PAD_BTN_L2;
        if (st->r2 >= PAD_TRIGGER_DIGITAL_THRESHOLD) st->buttons |= PAD_BTN_R2;

        /* 16-bit signed sticks -> 8-bit unsigned */
        int16_t sx = (int16_t)(data[off + 4] | ((uint16_t)data[off + 5] << 8));
        int16_t sy = (int16_t)(data[off + 6] | ((uint16_t)data[off + 7] << 8));
        int16_t rx = (int16_t)(data[off + 8] | ((uint16_t)data[off + 9] << 8));
        int16_t ry = (int16_t)(data[off + 10] | ((uint16_t)data[off + 11] << 8));

        st->lx = axis16_to_u8(sx, 0);
        st->ly = axis16_to_u8(sy, 1);
        st->rx = axis16_to_u8(rx, 0);
        st->ry = axis16_to_u8(ry, 1);
        return 1;
    }

    case CTRL_SONY_DS3: {
        /* DualShock 3 standard HID report */
        if (len < 10) return 0;
        uint8_t b1 = data[2];
        uint8_t b2 = data[3];

        if (b1 & 0x01) st->buttons |= PAD_BTN_SHARE;   /* Select */
        if (b1 & 0x02) st->buttons |= PAD_BTN_L3;
        if (b1 & 0x04) st->buttons |= PAD_BTN_R3;
        if (b1 & 0x08) st->buttons |= PAD_BTN_OPTIONS; /* Start */
        if (b1 & 0x10) st->buttons |= PAD_DPAD_UP;
        if (b1 & 0x20) st->buttons |= PAD_DPAD_RIGHT;
        if (b1 & 0x40) st->buttons |= PAD_DPAD_DOWN;
        if (b1 & 0x80) st->buttons |= PAD_DPAD_LEFT;

        if (b2 & 0x01) { st->buttons |= PAD_BTN_L2; st->l2 = 255; }
        if (b2 & 0x02) { st->buttons |= PAD_BTN_R2; st->r2 = 255; }
        if (b2 & 0x04) st->buttons |= PAD_BTN_L1;
        if (b2 & 0x08) st->buttons |= PAD_BTN_R1;
        if (b2 & 0x10) st->buttons |= PAD_BTN_TRIANGLE;
        if (b2 & 0x20) st->buttons |= PAD_BTN_CIRCLE;
        if (b2 & 0x40) st->buttons |= PAD_BTN_CROSS;
        if (b2 & 0x80) st->buttons |= PAD_BTN_SQUARE;

        if (len > 4 && (data[4] & 0x01)) st->buttons |= PAD_BTN_PS;

        if (len >= 10) {
            st->lx = data[6];
            st->ly = data[7];
            st->rx = data[8];
            st->ry = data[9];
        }
        return 1;
    }

    default: {
        /* Robust generic HID gamepad fallback */
        int off = (len >= 7 && data[0] <= 0x04) ? 1 : 0;
        if (len >= (size_t)(off + 5)) {
            st->lx = data[off + 0];
            st->ly = data[off + 1];
            st->rx = (len >= (size_t)(off + 4)) ? data[off + 2] : 128;
            st->ry = (len >= (size_t)(off + 4)) ? data[off + 3] : 128;

            uint8_t hat = data[off + 4] & 0x0F;
            switch (hat) {
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

            if (len >= (size_t)(off + 6)) {
                uint16_t b = (uint16_t)(data[off + 4] >> 4) | ((uint16_t)data[off + 5] << 4);
                if (len >= (size_t)(off + 7)) {
                    b |= ((uint16_t)data[off + 6] << 12);
                }
                if (b & 0x01) st->buttons |= PAD_BTN_CROSS;
                if (b & 0x02) st->buttons |= PAD_BTN_CIRCLE;
                if (b & 0x04) st->buttons |= PAD_BTN_SQUARE;
                if (b & 0x08) st->buttons |= PAD_BTN_TRIANGLE;
                if (b & 0x10) st->buttons |= PAD_BTN_L1;
                if (b & 0x20) st->buttons |= PAD_BTN_R1;
                if (b & 0x40) { st->buttons |= PAD_BTN_L2; st->l2 = 255; }
                if (b & 0x80) { st->buttons |= PAD_BTN_R2; st->r2 = 255; }
                if (b & 0x0100) st->buttons |= PAD_BTN_SHARE;   /* Select / Back */
                if (b & 0x0200) st->buttons |= PAD_BTN_OPTIONS; /* Start */
                if (b & 0x0400) st->buttons |= PAD_BTN_L3;
                if (b & 0x0800) st->buttons |= PAD_BTN_R3;
                if (b & 0x1000) st->buttons |= PAD_BTN_PS;      /* Home / Guide */
            }
            return 1;
        }
        break;
    }
        break;
    }
    return 0;
}
