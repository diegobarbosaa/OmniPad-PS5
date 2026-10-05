#ifndef VERSION_H
#define VERSION_H

#define ANYPAD_NAME             "OmniPad PS5"
#define ANYPAD_VERSION          "1.0.4"
#define ANYPAD_BUILD            5
#define ANYPAD_AUTHOR           "Diego Lima & PS5 Scene Community"
#define ANYPAD_DESCRIPTION      "Plug & Play USB & 2.4G Dongle Gamepad Engine for PS5 (FW 7.00 - 13.60)"

/* Supported firmware bounds: Relapse exploit enables 7.00 through 13.60 */
#define ANYPAD_MIN_FW           0x07000000  /* 7.00 */
#define ANYPAD_MAX_FW           0x1360FFFF  /* 13.60.xx */

#define ANYPAD_DEFAULT_PORT     8095
#define ANYPAD_TCP_STREAM_PORT  9045

#endif /* VERSION_H */
