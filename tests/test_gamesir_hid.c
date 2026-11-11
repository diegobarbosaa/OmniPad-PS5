#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/usb_controllers.h"
static void check_gamesir(void) {
    const char *name=NULL;
    assert(usb_identify_controller(0x3537,0x1022,&name)==CTRL_GAMESIR_G7PRO);
    assert(strstr(name,"G7 Pro"));
    assert(usb_identify_controller(0x3537,0x1082,&name)==CTRL_GAMESIR_G7SE);
    assert(usb_identify_controller(0x3537,0x1010,&name)==CTRL_XBOX_XINPUT);
    assert(usb_identify_controller(0x2f24,0x0001,&name)==CTRL_XBOX_XINPUT);
    uint8_t pro[9] = {0x89, 0x66, 0x01, 128, 127, 129, 128, 37, 201};
    pad_state_t st;
    assert(usb_parse_input_report(CTRL_GAMESIR_G7PRO,pro,9,&st));
    assert((st.buttons & (PAD_BTN_CROSS|PAD_BTN_SQUARE|PAD_BTN_R1|PAD_DPAD_UP|PAD_DPAD_RIGHT|PAD_BTN_L3|PAD_BTN_R3)) ==
            (PAD_BTN_CROSS|PAD_BTN_SQUARE|PAD_BTN_R1|PAD_DPAD_UP|PAD_DPAD_RIGHT|PAD_BTN_L3|PAD_BTN_R3));
    assert(st.lx==128 && st.ly==127 && st.rx==129 && st.l2==37 && st.r2==201);
    assert(!usb_parse_input_report(CTRL_GAMESIR_G7PRO,pro,8,&st));
    uint8_t se[10] = {0x05, 0, 0, 0x08, 128, 128, 128, 128, 0, 0};
    assert(usb_parse_input_report(CTRL_GAMESIR_G7SE,se,10,&st));
    assert(st.buttons==0);
    se[0]=0x06;
    assert(!usb_parse_input_report(CTRL_GAMESIR_G7SE,se,10,&st));
    assert(!usb_parse_input_report(CTRL_GAMESIR_G7SE,se,9,&st));
}

int main(void){check_gamesir();puts("GameSir HID tests passed.");}
