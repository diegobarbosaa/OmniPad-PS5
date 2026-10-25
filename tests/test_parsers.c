#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../src/pad_types.h"
#include "../src/usb_controllers.h"
#include "../src/profiles.h"

static void test_ds4_usb_parsing(void)
{
    printf("Testing DualShock 4 USB parsing... ");
    uint8_t report[64];
    memset(report, 0, sizeof(report));
    report[0] = 0x01; /* report ID */
    report[1] = 0x80; /* lx */
    report[2] = 0x80; /* ly */
    report[3] = 0x80; /* rx */
    report[4] = 0x80; /* ry */
    report[5] = 0x20; /* Cross pressed */
    report[6] = 0x01; /* L1 pressed */
    report[7] = 0x01; /* PS button pressed */
    report[8] = 0xFF; /* L2 trigger full */
    report[9] = 0x80; /* R2 trigger half */

    pad_state_t st;
    int res = usb_parse_input_report(CTRL_SONY_DS4, report, sizeof(report), &st);
    assert(res == 1);
    assert(st.buttons & PAD_BTN_CROSS);
    assert(st.buttons & PAD_BTN_L1);
    assert(st.buttons & PAD_BTN_PS);
    assert(st.l2 == 255);
    assert(st.r2 == 128);
    printf("OK!\n");
}

static void test_switch_pro_usb_parsing(void)
{
    printf("Testing Switch Pro USB parsing... ");
    uint8_t report[64];
    memset(report, 0, sizeof(report));
    report[3] = 0x01 | 0x40; /* B (Cross) + L1 */
    report[4] = 0x10;        /* Home (PS button) */
    report[5] = 0x02;        /* Dpad UP */

    /* Sticks at bytes 6..11 */
    report[6] = 0x00;
    report[7] = 0x80;
    report[8] = 0x80;
    report[9] = 0x00;
    report[10] = 0x80;
    report[11] = 0x80;

    pad_state_t st;
    int res = usb_parse_input_report(CTRL_NINTENDO_SWITCH_PRO, report, sizeof(report), &st);
    assert(res == 1);
    assert(st.buttons & PAD_BTN_CROSS);
    assert(st.buttons & PAD_BTN_L1);
    assert(st.buttons & PAD_BTN_PS);
    assert(st.buttons & PAD_DPAD_UP);
    printf("OK!\n");
}

static void test_xbox_xinput_parsing(void)
{
    printf("Testing Xbox / Machenike XInput parsing... ");
    uint8_t report[20];
    memset(report, 0, sizeof(report));
    report[0] = 0x00;
    report[1] = 0x14;
    report[2] = 0x00;
    report[3] = 0x14; /* A button (0x1000) + Guide/PS (0x0400) */
    report[4] = 200;  /* L2 */
    report[5] = 250;  /* R2 */

    pad_state_t st;
    int res = usb_parse_input_report(CTRL_XBOX_XINPUT, report, sizeof(report), &st);
    assert(res == 1);
    assert(st.buttons & PAD_BTN_CROSS);
    assert(st.buttons & PAD_BTN_PS);
    assert(st.l2 == 200);
    assert(st.r2 == 250);
    printf("OK!\n");
}

static void test_xbox_one_gip_parsing(void)
{
    printf("Testing Xbox One / Machenike GIP (0x20) report parsing... ");
    uint8_t report[20];
    memset(report, 0, sizeof(report));
    report[0] = 0x20; /* GIP report ID */
    report[1] = 0x00;
    report[2] = 0x01; /* Sequence */
    report[3] = 0x0E; /* Payload length */

    /* Buttons: A (0x0010) + D-pad Up (0x0100) + LB/L1 (0x1000) */
    uint16_t buttons = 0x0010 | 0x0100 | 0x1000;
    report[4] = (uint8_t)(buttons & 0xFF);
    report[5] = (uint8_t)(buttons >> 8);

    /* Triggers: L2 = 512 (128), R2 = 1023 (255) */
    uint16_t l2 = 512;
    uint16_t r2 = 1023;
    report[6] = (uint8_t)(l2 & 0xFF);
    report[7] = (uint8_t)(l2 >> 8);
    report[8] = (uint8_t)(r2 & 0xFF);
    report[9] = (uint8_t)(r2 >> 8);

    /* Sticks: Centered (0) */
    report[10] = 0x00; report[11] = 0x00;
    report[12] = 0x00; report[13] = 0x00;
    report[14] = 0x00; report[15] = 0x00;
    report[16] = 0x00; report[17] = 0x00;

    pad_state_t st;
    int res = usb_parse_input_report(CTRL_XBOX_XINPUT, report, sizeof(report), &st);
    assert(res == 1);
    assert(st.buttons & PAD_BTN_CROSS);
    assert(st.buttons & PAD_DPAD_UP);
    assert(st.buttons & PAD_BTN_L1);
    assert(st.buttons & PAD_BTN_L2);
    assert(st.buttons & PAD_BTN_R2);
    assert(st.l2 == 128);
    assert(st.r2 == 255);
    assert(st.lx == 128);
    assert(st.ly == 128);
    assert(st.rx == 128);
    assert(st.ry == 128);
    printf("OK!\n");
}

static void test_xbox_one_guide_parsing(void)
{
    printf("Testing Xbox One Guide packet (0x07) parsing... ");
    uint8_t report[6] = { 0x07, 0x20, 0x01, 0x02, 0x5B, 0x01 };
    pad_state_t st;
    int res = usb_parse_input_report(CTRL_XBOX_XINPUT, report, sizeof(report), &st);
    assert(res == 1);
    assert(st.buttons & PAD_BTN_PS);
    printf("OK!\n");
}

static void test_machenike_identification(void)
{
    printf("Testing Machenike G5 Pro Max SE identification... ");
    const char *name = NULL;
    /* 1. PC Mode (0x045E:0x028E) */
    usb_controller_type_t t1 = usb_identify_controller(0x045e, 0x028e, &name);
    assert(t1 == CTRL_XBOX_XINPUT);
    assert(strstr(name, "Machenike") != NULL || strstr(name, "Xbox") != NULL);

    /* 2. Machenike Native VID Mode (0x2F24:0x0001) */
    usb_controller_type_t t2 = usb_identify_controller(0x2f24, 0x0001, &name);
    assert(t2 == CTRL_XBOX_XINPUT);
    assert(strstr(name, "MACHENIKE") != NULL);

    /* 3. NS Switch Mode (0x057E:0x2009) */
    usb_controller_type_t t3 = usb_identify_controller(0x057e, 0x2009, &name);
    assert(t3 == CTRL_NINTENDO_SWITCH_PRO);
    printf("OK!\n");
}

static void test_ds3_usb_parsing(void)
{
    printf("Testing DualShock 3 USB parsing... ");
    uint8_t report[49];
    memset(report, 0, sizeof(report));
    report[2] = 0x08; /* Start (Options) */
    report[3] = 0x40; /* Cross */
    report[4] = 0x01; /* PS button */
    report[6] = 128;  /* lx */
    report[7] = 128;  /* ly */

    pad_state_t st;
    int res = usb_parse_input_report(CTRL_SONY_DS3, report, sizeof(report), &st);
    assert(res == 1);
    assert(st.buttons & PAD_BTN_OPTIONS);
    assert(st.buttons & PAD_BTN_CROSS);
    assert(st.buttons & PAD_BTN_PS);
    printf("OK!\n");
}

static void test_shadowmount_storage_isolation(void)
{
    printf("Testing ShadowMountPlus USB storage drive isolation... ");
    const char *name = NULL;

    /* 1. SanDisk Cruzer / Ultra (0x0781:0x5583) */
    usb_controller_type_t t1 = usb_identify_controller(0x0781, 0x5583, &name);
    assert(t1 == CTRL_UNKNOWN);

    /* 2. Kingston DataTraveler (0x0951:0x1666) */
    usb_controller_type_t t2 = usb_identify_controller(0x0951, 0x1666, &name);
    assert(t2 == CTRL_UNKNOWN);

    /* 3. Seagate Expansion Portable HDD (0x0BC2:0xAB24) */
    usb_controller_type_t t3 = usb_identify_controller(0x0bc2, 0xab24, &name);
    assert(t3 == CTRL_UNKNOWN);

    /* 4. Sony External USB Drive (0x054C:0x0B8F) - must NOT be grabbed as gamepad */
    usb_controller_type_t t4 = usb_identify_controller(0x054c, 0x0b8f, &name);
    assert(t4 == CTRL_UNKNOWN);

    printf("OK!\n");
}

static void test_machenike_g5_pro_pc_mode_zero_byte1(void)
{
    printf("Testing Machenike G5 Pro PC mode (byte 1 = 0x00)... ");
    uint8_t report[20];
    memset(report, 0, sizeof(report));
    report[0] = 0x00;
    report[1] = 0x00; /* Clones report 0x00 instead of 0x14 */
    report[2] = 0x02; /* D-pad Down */
    report[3] = 0x10; /* Button A -> Cross */

    pad_state_t st;
    int res = usb_parse_input_report(CTRL_XBOX_XINPUT, report, sizeof(report), &st);
    assert(res == 1);
    assert(st.buttons & PAD_BTN_CROSS);
    assert(st.buttons & PAD_DPAD_DOWN);
    printf("OK!\n");
}

static void test_machenike_g5_pro_hid_mode(void)
{
    printf("Testing Machenike G5 Pro HID fallback report... ");
    uint8_t report[64];
    memset(report, 0, sizeof(report));
    report[0] = 0x01;        /* Report ID */
    report[1] = 128;         /* LX */
    report[2] = 128;         /* LY */
    report[3] = 128;         /* RX */
    report[4] = 128;         /* RY */
    report[5] = 0x04 | 0x10; /* Hat switch 4 (Down) + Button 1 (Cross) */

    pad_state_t st;
    int res = usb_parse_input_report(CTRL_XBOX_XINPUT, report, sizeof(report), &st);
    assert(res == 1);
    assert(st.buttons & PAD_BTN_CROSS);
    assert(st.buttons & PAD_DPAD_DOWN);
    printf("OK!\n");
}

static void test_machenike_g5_pro_wired_usb_c(void)
{
    printf("Testing Machenike G5 Pro Wired USB-C Mode (VID 0x2345)... ");
    const char *name = NULL;

    /* 1. PC Mode via USB-C Cable (0x2345:0xE02B) */
    usb_controller_type_t t1 = usb_identify_controller(0x2345, 0xe02b, &name);
    assert(t1 == CTRL_XBOX_XINPUT);
    assert(name != NULL && (strstr(name, "MACHENIKE") != NULL || strstr(name, "Machenike") != NULL));

    /* 2. Standard Wired USB Mode (0x2345:0xE00B) */
    usb_controller_type_t t2 = usb_identify_controller(0x2345, 0xe00b, &name);
    assert(t2 == CTRL_XBOX_XINPUT);
    assert(name != NULL && (strstr(name, "MACHENIKE") != NULL || strstr(name, "Machenike") != NULL));

    /* 3. Machenike G5 Pro Max Wired (0x2345:0xE02E) */
    usb_controller_type_t t3 = usb_identify_controller(0x2345, 0xe02e, &name);
    assert(t3 == CTRL_XBOX_XINPUT);
    assert(name != NULL && (strstr(name, "MACHENIKE") != NULL || strstr(name, "Machenike") != NULL));

    /* 4. Machenike G5 Pro 2.4G Receiver (0x2345:0xE02F) */
    usb_controller_type_t t4 = usb_identify_controller(0x2345, 0xe02f, &name);
    assert(t4 == CTRL_XBOX_XINPUT);
    assert(name != NULL && (strstr(name, "MACHENIKE") != NULL || strstr(name, "Machenike") != NULL));

    /* 5. Test parsing report with vid=0x2345 */
    uint8_t report[20];
    memset(report, 0, sizeof(report));
    report[0] = 0x01; /* Report ID */
    report[1] = 0x00; report[2] = 0x80; /* LX = 128 */
    report[3] = 0x00; report[4] = 0x80; /* LY = 128 */
    report[5] = 0x00; report[6] = 0x80; /* RX = 128 */
    report[7] = 0x00; report[8] = 0x80; /* RY = 128 */
    report[15] = 0x01; /* Button A -> Cross */

    pad_state_t st;
    int res = profiles_parse_report(0x2345, 0xe02b, report, sizeof(report), &st);
    assert(res == 1);
    assert(st.buttons & PAD_BTN_CROSS);

    printf("OK!\n");
}

static void test_dongle_standby_isolation(void)
{
    printf("Testing 2.4G Dongle receiver identification (0x1A34:0xF517)... ");
    const char *name = NULL;

    /* 2.4G Dongle receiver must be claimed as active XInput controller */
    usb_controller_type_t t = usb_identify_controller(0x1a34, 0xf517, &name);
    assert(t == CTRL_XBOX_XINPUT);
    assert(name != NULL && strstr(name, "Dongle") != NULL);

    printf("OK!\n");
}

/* Build a T598 report 0x07 from the first 19 data bytes of a real capture */
static void t598_report(uint8_t *out, const uint8_t *d19)
{
    memset(out, 0, 64);
    out[0] = 0x07;
    memcpy(out + 1, d19, 19);
}

static void test_thrustmaster_t598(void)
{
    printf("Testing Thrustmaster T598 wheel (real captured reports)... ");
    const char *name = NULL;
    assert(usb_identify_controller(0x044f, 0xb696, &name) == CTRL_THRUSTMASTER_T598);
    assert(name != NULL && strstr(name, "T598") != NULL);

    static const uint8_t rest[19]   = {0x54,0x80,0xff,0x03,0xff,0x03,0xff,0x03,0,0,0x80,0x80,0x80,0x80,0,0,0,0,0x08};
    static const uint8_t left[19]   = {0x00,0x00,0xff,0x03,0xff,0x03,0xff,0x03,0,0,0x80,0x80,0x80,0x80,0,0,0,0,0x08};
    static const uint8_t right[19]  = {0xff,0xff,0xff,0x03,0xff,0x03,0xff,0x03,0,0,0x80,0x80,0x80,0x80,0,0,0,0,0x08};
    static const uint8_t gas[19]    = {0x69,0xe6,0xff,0x03,0x00,0x00,0xff,0x03,0,0,0x80,0x80,0x80,0x80,0,0,0,0,0x08};
    static const uint8_t brake[19]  = {0x69,0xe6,0x00,0x00,0xff,0x03,0xff,0x03,0,0,0x80,0x80,0x80,0x80,0,0,0,0,0x08};
    static const uint8_t paddle[19] = {0x8a,0xe7,0xff,0x03,0xff,0x03,0xff,0x03,0,0,0x80,0x80,0x80,0x80,0x01,0,0,0,0x08};

    uint8_t r[64];
    pad_state_t st;

    t598_report(r, rest);
    assert(usb_parse_input_report(CTRL_THRUSTMASTER_T598, r, sizeof(r), &st) == 1);
    assert(st.lx == 128 && st.ly == 128 && st.l2 == 0 && st.r2 == 0 && st.buttons == 0);

    t598_report(r, left);
    usb_parse_input_report(CTRL_THRUSTMASTER_T598, r, sizeof(r), &st);
    assert(st.lx == 0);

    t598_report(r, right);
    usb_parse_input_report(CTRL_THRUSTMASTER_T598, r, sizeof(r), &st);
    assert(st.lx == 255);

    t598_report(r, gas);
    usb_parse_input_report(CTRL_THRUSTMASTER_T598, r, sizeof(r), &st);
    assert(st.r2 == 255 && st.l2 == 0);

    t598_report(r, brake);
    usb_parse_input_report(CTRL_THRUSTMASTER_T598, r, sizeof(r), &st);
    assert(st.l2 == 255 && st.r2 == 0);

    /* A small turn just past center must already move the stick past the game's deadzone */
    t598_report(r, rest);
    r[1] = 0x00; r[2] = 0x82; /* 0x8200 = 512 units right of center */
    usb_parse_input_report(CTRL_THRUSTMASTER_T598, r, sizeof(r), &st);
    assert(st.lx >= 128 + (128 * T598_STEER_ANTIDEADZONE_PCT / 100));
    r[1] = 0x00; r[2] = 0x7e; /* 512 units left */
    usb_parse_input_report(CTRL_THRUSTMASTER_T598, r, sizeof(r), &st);
    assert(st.lx <= 128 - (128 * T598_STEER_ANTIDEADZONE_PCT / 100));

    t598_report(r, paddle);
    usb_parse_input_report(CTRL_THRUSTMASTER_T598, r, sizeof(r), &st);
    assert(st.buttons == PAD_BTN_L1);

    /* Buttons from the step-4 capture: cross = byte 14 bit 5, PS = byte 15 bit 4, R3 = byte 16 bit 6 */
    t598_report(r, rest);
    r[1 + 14] = 0x20; r[1 + 15] = 0x10; r[1 + 16] = 0x40; r[1 + 18] = 0x02; /* + d-pad right */
    usb_parse_input_report(CTRL_THRUSTMASTER_T598, r, sizeof(r), &st);
    assert(st.buttons == (PAD_BTN_CROSS | PAD_BTN_PS | PAD_BTN_R3 | PAD_DPAD_RIGHT));

    /* Vendor / force-feedback reports must be ignored */
    memset(r, 0x55, sizeof(r));
    r[0] = 0x02;
    assert(usb_parse_input_report(CTRL_THRUSTMASTER_T598, r, sizeof(r), &st) == 0);
    printf("OK!\n");
}

int main(void)
{
    printf("========================================\n");
    printf("Running OmniPad PS5 Parser Tests\n");
    printf("========================================\n");

    test_ds4_usb_parsing();
    test_switch_pro_usb_parsing();
    test_xbox_xinput_parsing();
    test_machenike_g5_pro_pc_mode_zero_byte1();
    test_machenike_g5_pro_hid_mode();
    test_xbox_one_gip_parsing();
    test_xbox_one_guide_parsing();
    test_machenike_identification();
    test_machenike_g5_pro_wired_usb_c();
    test_dongle_standby_isolation();
    test_ds3_usb_parsing();
    test_shadowmount_storage_isolation();
    test_thrustmaster_t598();

    printf("\nAll parser tests passed successfully!\n");
    return 0;
}
