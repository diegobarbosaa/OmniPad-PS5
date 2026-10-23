#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/usb_controllers.h"

int main(void) {
    const char *name = NULL;
    assert(usb_identify_controller(0x0738, 0x8828, &name) == CTRL_MADCATZ_SF4_FIGHTPAD);
    assert(name && strstr(name, "Mad Catz"));
    /* Other Mad Catz IDs keep the generic classification. */
    assert(usb_identify_controller(0x0738, 0x4716, &name) == CTRL_GENERIC_HID);

    uint8_t r[27]; pad_state_t st;
    memset(r, 0, sizeof(r)); r[2] = 0x0f;                 /* neutral */
    assert(usb_parse_input_report(CTRL_MADCATZ_SF4_FIGHTPAD, r, sizeof(r), &st) == 1);
    assert(st.buttons == 0 && st.lx == 128 && st.ly == 128);

    /* Face/shoulder byte 0 */
    const struct { uint8_t m; uint32_t b; } f[] = {
        {0x01, PAD_BTN_SQUARE}, {0x02, PAD_BTN_CROSS}, {0x04, PAD_BTN_CIRCLE}, {0x08, PAD_BTN_TRIANGLE},
        {0x10, PAD_BTN_L1}, {0x20, PAD_BTN_R1}, {0x40, PAD_BTN_L2}, {0x80, PAD_BTN_R2} };
    for (unsigned i = 0; i < sizeof(f)/sizeof(f[0]); i++) {
        memset(r, 0, sizeof(r)); r[2] = 0x0f; r[0] = f[i].m;
        assert(usb_parse_input_report(CTRL_MADCATZ_SF4_FIGHTPAD, r, sizeof(r), &st));
        assert(st.buttons == f[i].b);
    }
    /* System byte 1 */
    memset(r, 0, sizeof(r)); r[2] = 0x0f; r[1] = 0x13;
    assert(usb_parse_input_report(CTRL_MADCATZ_SF4_FIGHTPAD, r, sizeof(r), &st));
    assert(st.buttons == (PAD_BTN_SHARE | PAD_BTN_OPTIONS | PAD_BTN_PS));
    /* Captured cardinals */
    const struct { uint8_t v; uint32_t b; } d[] = {
        {0, PAD_DPAD_UP}, {2, PAD_DPAD_RIGHT}, {4, PAD_DPAD_DOWN}, {6, PAD_DPAD_LEFT} };
    for (unsigned i = 0; i < 4; i++) {
        memset(r, 0, sizeof(r)); r[2] = d[i].v;
        assert(usb_parse_input_report(CTRL_MADCATZ_SF4_FIGHTPAD, r, sizeof(r), &st));
        assert(st.buttons == d[i].b);
    }
    assert(!usb_parse_input_report(CTRL_MADCATZ_SF4_FIGHTPAD, r, 2, &st));
    puts("Mad Catz SF IV FightPad tests passed.");
    return 0;
}
