#include "tcp_frames.h"

#include <string.h>

static const uint32_t VALID_BUTTONS =
    PAD_BTN_SHARE | PAD_BTN_L3 | PAD_BTN_R3 | PAD_BTN_OPTIONS |
    PAD_DPAD_UP | PAD_DPAD_RIGHT | PAD_DPAD_DOWN | PAD_DPAD_LEFT |
    PAD_BTN_L2 | PAD_BTN_R2 | PAD_BTN_L1 | PAD_BTN_R1 |
    PAD_BTN_TRIANGLE | PAD_BTN_CIRCLE | PAD_BTN_CROSS | PAD_BTN_SQUARE |
    PAD_BTN_PS | PAD_BTN_MUTE | PAD_BTN_TOUCHPAD;

int tcp_stream_decode_frame(const uint8_t *frame, size_t length,
                            pad_state_t *state)
{
    uint32_t buttons;

    if (!frame || !state || length != TCP_STREAM_FRAME_SIZE) return 0;
    for (size_t i = 10; i < TCP_STREAM_FRAME_SIZE; i++) {
        if (frame[i] != 0) return 0;
    }

    buttons = (uint32_t)frame[0] |
              ((uint32_t)frame[1] << 8) |
              ((uint32_t)frame[2] << 16) |
              ((uint32_t)frame[3] << 24);
    if ((buttons & ~VALID_BUTTONS) != 0) return 0;

    pad_state_neutral(state);
    state->buttons = buttons;
    state->lx = frame[4];
    state->ly = frame[5];
    state->rx = frame[6];
    state->ry = frame[7];
    state->l2 = frame[8];
    state->r2 = frame[9];
    state->conn_type = CONN_NETWORK_STREAM;
    return 1;
}

int tcp_stream_feed(tcp_frame_buffer_t *buffer, const uint8_t *data,
                    size_t length, tcp_frame_callback_t callback,
                    void *context)
{
    size_t offset = 0;

    if (!buffer || (!data && length != 0) || !callback ||
        buffer->used >= TCP_STREAM_FRAME_SIZE) return 0;

    while (offset < length) {
        size_t available = TCP_STREAM_FRAME_SIZE - buffer->used;
        size_t remaining = length - offset;
        size_t copy_len = available < remaining ? available : remaining;
        memcpy(buffer->bytes + buffer->used, data + offset, copy_len);
        buffer->used += copy_len;
        offset += copy_len;

        if (buffer->used == TCP_STREAM_FRAME_SIZE) {
            pad_state_t state;
            if (!tcp_stream_decode_frame(buffer->bytes, sizeof(buffer->bytes), &state)) {
                buffer->used = 0;
                return 0;
            }
            buffer->used = 0;
            if (!callback(&state, context)) return 0;
        }
    }
    return 1;
}
