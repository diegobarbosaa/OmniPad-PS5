#include <assert.h>
#include <stdio.h>
#include "../src/usb_controllers.h"
/* Literal semantics from upstream b7682ef; not B26's alternative formula. */
static uint8_t upstream_y(uint16_t raw) {
    uint8_t y_u8 = (uint8_t)(raw >> 4);
    return (y_u8 == 128) ? 128 : (uint8_t)(255 - y_u8);
}
int main(void) {
    const usb_controller_type_t paths[] = {CTRL_NINTENDO_SWITCH_PRO};
    for (unsigned p=0;p<sizeof(paths)/sizeof(paths[0]);p++) {
        for (unsigned x=0;x<4096;x++) {
            uint8_t r[64]={0x30};
            r[6]=r[9]=(uint8_t)x;
            r[7]=r[10]=(uint8_t)(((x>>8)&15)|((x&15)<<4));
            r[8]=r[11]=(uint8_t)(x>>4);
            pad_state_t st;
            assert(usb_parse_input_report(paths[p],r,sizeof(r),&st));
            assert(st.ly==upstream_y(x) && st.ry==upstream_y(x));
            assert(st.lx==(x>>4) && st.rx==(x>>4));
        }
    }
    uint8_t invalid[64]={0x81};pad_state_t state;
    assert(!usb_parse_input_report(CTRL_NINTENDO_SWITCH_PRO,invalid,64,&state));
    invalid[0]=0x21;assert(!usb_parse_input_report(CTRL_NINTENDO_SWITCH_PRO,invalid,64,&state));
    invalid[0]=0x30;assert(!usb_parse_input_report(CTRL_NINTENDO_SWITCH_PRO,invalid,11,&state));
    invalid[3]=0xcf;invalid[4]=0x3f;invalid[5]=0xcf;
    assert(usb_parse_input_report(CTRL_NINTENDO_SWITCH_PRO,invalid,64,&state));
    assert((state.buttons & (PAD_BTN_CROSS|PAD_BTN_CIRCLE|PAD_BTN_SQUARE|PAD_BTN_TRIANGLE|PAD_BTN_L1|PAD_BTN_R1|PAD_BTN_L2|PAD_BTN_R2|PAD_BTN_PS|PAD_BTN_TOUCHPAD|PAD_BTN_SHARE|PAD_BTN_OPTIONS|PAD_BTN_L3|PAD_BTN_R3)) ==
           (PAD_BTN_CROSS|PAD_BTN_CIRCLE|PAD_BTN_SQUARE|PAD_BTN_TRIANGLE|PAD_BTN_L1|PAD_BTN_R1|PAD_BTN_L2|PAD_BTN_R2|PAD_BTN_PS|PAD_BTN_TOUCHPAD|PAD_BTN_SHARE|PAD_BTN_OPTIONS|PAD_BTN_L3|PAD_BTN_R3));
    puts("Upstream Y restoration: all 4096 values on both sticks/Nintendo path passed.");
}
