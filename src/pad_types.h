#ifndef PAD_TYPES_H
#define PAD_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define MAX_SLOTS 3

/* ---- PS5 DualSense Buttons bitmask ---- */
#define PAD_BTN_CREATE      (1u << 0)
#define PAD_BTN_SHARE       (1u << 0)
#define PAD_BTN_L3          (1u << 1)
#define PAD_BTN_R3          (1u << 2)
#define PAD_BTN_OPTIONS     (1u << 3)
#define PAD_DPAD_UP         (1u << 4)
#define PAD_DPAD_RIGHT      (1u << 5)
#define PAD_DPAD_DOWN       (1u << 6)
#define PAD_DPAD_LEFT       (1u << 7)
#define PAD_BTN_L2          (1u << 8)
#define PAD_BTN_R2          (1u << 9)
#define PAD_BTN_L1          (1u << 10)
#define PAD_BTN_R1          (1u << 11)
#define PAD_BTN_TRIANGLE    (1u << 12)
#define PAD_BTN_CIRCLE      (1u << 13)
#define PAD_BTN_CROSS       (1u << 14)
#define PAD_BTN_SQUARE      (1u << 15)
#define PAD_BTN_PS          (1u << 16)
#define PAD_BTN_MUTE        (1u << 18)
#define PAD_BTN_TOUCHPAD    (1u << 20)

#define PAD_TRIGGER_DIGITAL_THRESHOLD 30

/* ---- PadTouch: 8 bytes (natural alignment) ---- */
typedef struct PadTouch {
    uint16_t x;
    uint16_t y;
    uint8_t  id;
    uint8_t  reserved[3];
} PadTouch;

/* ---- PadData: Exact 120-byte controller report ABI accepted by PS5 kernel / libScePad ----
 * Verified against public PS5 pad reverse engineering and YetAnotherControllerEnabler:
 * buttons at 0x00, touch at 0x34, connected at 0x4C, timestamp at 0x50. Total size: 120 (0x78) bytes.
 * orient_w MUST be 1.0f (quaternion identity); otherwise kernel discards degenerate frame.
 */
typedef struct PadData {
    uint32_t buttons;               /* 0x00 (4 bytes) */
    uint8_t  left_x;                /* 0x04 (1 byte) */
    uint8_t  left_y;                /* 0x05 (1 byte) */
    uint8_t  right_x;               /* 0x06 (1 byte) */
    uint8_t  right_y;               /* 0x07 (1 byte) */
    uint8_t  l2;                    /* 0x08 (1 byte) */
    uint8_t  r2;                    /* 0x09 (1 byte) */
    uint8_t  analog_reserved[2];    /* 0x0A (2 bytes) */
    float    orient_x;              /* 0x0C (4 bytes) */
    float    orient_y;              /* 0x10 (4 bytes) */
    float    orient_z;              /* 0x14 (4 bytes) */
    float    orient_w;              /* 0x18 (4 bytes) -> MUST BE 1.0f! */
    float    accel_x;               /* 0x1C (4 bytes) */
    float    accel_y;               /* 0x20 (4 bytes) */
    float    accel_z;               /* 0x24 (4 bytes) */
    float    angvel_x;              /* 0x28 (4 bytes) */
    float    angvel_y;              /* 0x2C (4 bytes) */
    float    angvel_z;              /* 0x30 (4 bytes) */
    uint8_t  touch_count;           /* 0x34 (1 byte) */
    uint8_t  touch_reserved[7];     /* 0x35 (7 bytes) */
    PadTouch touch[2];              /* 0x3C (16 bytes) */
    int32_t  connected;             /* 0x4C (4 bytes) -> MUST BE 1 */
    uint64_t timestamp_us;          /* 0x50 (8 bytes) */
    uint32_t extension_unit_id;     /* 0x58 (4 bytes) */
    uint8_t  extension_reserved;    /* 0x5C (1 byte) */
    uint8_t  extension_length;      /* 0x5D (1 byte) */
    uint8_t  extension_data[10];    /* 0x5E (10 bytes) */
    uint8_t  connected_count;       /* 0x68 (1 byte) -> MUST BE 1 */
    uint8_t  reserved[2];           /* 0x69 (2 bytes) */
    uint8_t  device_unique_length;  /* 0x6B (1 byte) */
    uint8_t  device_unique_data[12];/* 0x6C (12 bytes) */
} PadData;

/* Backwards compatibility alias */
typedef PadData ScePadData;

typedef enum {
    CONN_NONE = 0,
    CONN_BLUETOOTH_CLASSIC,
    CONN_BLUETOOTH_LE,
    CONN_USB_WIRED,
    CONN_USB_DONGLE_24G,
    CONN_NETWORK_STREAM
} pad_conn_type_t;

/* ---- Internal normalized controller state ---- */
typedef struct {
    uint32_t        buttons;
    uint8_t         lx, ly;      /* 0-255, 128 = center */
    uint8_t         rx, ry;      /* 0-255, 128 = center */
    uint8_t         l2, r2;      /* 0-255, 0 = released */
    
    /* Touchpad & motion */
    uint8_t         touch_down;
    uint16_t        touch_x, touch_y;
    int16_t         accel[3];
    int16_t         gyro[3];

    /* Battery & Status */
    uint8_t         battery_level;   /* 0 - 100% */
    uint8_t         battery_charging;
    uint8_t         connected;
    pad_conn_type_t conn_type;
    char            controller_name[64];
    uint16_t        vid, pid;
    uint64_t        mac_or_dev_id;
    int32_t         assigned_user_id;
} pad_state_t;

static inline void pad_state_neutral(pad_state_t *st) {
    if (!st) return;
    memset(st, 0, sizeof(*st));
    st->lx = 128;
    st->ly = 128;
    st->rx = 128;
    st->ry = 128;
    st->l2 = 0;
    st->r2 = 0;
    st->accel[2] = 8192; /* 1G downward gravity */
    st->battery_level = 100;
}

static inline void pad_data_from_state(PadData *data, const pad_state_t *st, uint64_t timestamp_us) {
    if (!data) return;
    memset(data, 0, sizeof(*data));
    if (!st) return;

    data->buttons = st->buttons;
    data->left_x = st->lx;
    data->left_y = st->ly;
    data->right_x = st->rx;
    data->right_y = st->ry;
    data->l2 = st->l2;
    data->r2 = st->r2;
    if (st->l2 >= PAD_TRIGGER_DIGITAL_THRESHOLD) data->buttons |= PAD_BTN_L2;
    if (st->r2 >= PAD_TRIGGER_DIGITAL_THRESHOLD) data->buttons |= PAD_BTN_R2;

    /* Essential quaternion identity for PS5 kernel validation */
    data->orient_w = 1.0f;

    data->connected = 1;
    data->connected_count = 1;
    data->timestamp_us = timestamp_us;

    data->accel_x = (float)st->accel[0] / 8192.0f;
    data->accel_y = (float)st->accel[1] / 8192.0f;
    data->accel_z = (float)st->accel[2] / 8192.0f;
    data->angvel_x = (float)st->gyro[0] * (0.01745329252f / 1024.0f);
    data->angvel_y = (float)st->gyro[1] * (0.01745329252f / 1024.0f);
    data->angvel_z = (float)st->gyro[2] * (0.01745329252f / 1024.0f);

    if (st->touch_down) {
        data->touch_count = 1;
        data->touch[0].id = 1;
        data->touch[0].x = st->touch_x;
        data->touch[0].y = st->touch_y;
    }
}

#endif /* PAD_TYPES_H */
