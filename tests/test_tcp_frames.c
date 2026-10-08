#include "../src/tcp_frames.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int calls;
    pad_state_t last;
} capture_t;

static int capture_frame(const pad_state_t *state, void *context)
{
    capture_t *capture = (capture_t *)context;
    capture->calls++;
    capture->last = *state;
    return 1;
}

static void test_valid_and_fragmented_frames(void)
{
    uint8_t frame[TCP_STREAM_FRAME_SIZE] = {
        0x00, 0x40, 0x00, 0x00, /* Cross */
        0, 255, 128, 127, 30, 240,
        0, 0, 0, 0, 0, 0
    };
    tcp_frame_buffer_t buffer;
    capture_t capture;

    memset(&buffer, 0, sizeof(buffer));
    memset(&capture, 0, sizeof(capture));
    assert(tcp_stream_feed(&buffer, frame, 3, capture_frame, &capture));
    assert(capture.calls == 0);
    assert(buffer.used == 3);
    assert(tcp_stream_feed(&buffer, frame + 3, 5, capture_frame, &capture));
    assert(capture.calls == 0);
    assert(tcp_stream_feed(&buffer, frame + 8, sizeof(frame) - 8,
                           capture_frame, &capture));
    assert(capture.calls == 1);
    assert(buffer.used == 0);
    assert(capture.last.buttons == PAD_BTN_CROSS);
    assert(capture.last.lx == 0 && capture.last.ly == 255);
    assert(capture.last.rx == 128 && capture.last.ry == 127);
    assert(capture.last.l2 == 30 && capture.last.r2 == 240);
    assert(capture.last.conn_type == CONN_NETWORK_STREAM);
}

static void test_invalid_and_incomplete_frames(void)
{
    uint8_t frame[TCP_STREAM_FRAME_SIZE] = {0};
    pad_state_t state;
    tcp_frame_buffer_t buffer;
    capture_t capture;

    assert(!tcp_stream_decode_frame(frame, sizeof(frame) - 1u, &state));
    frame[0] = 0x00; frame[1] = 0x00; frame[2] = 0x00; frame[3] = 0x80;
    assert(!tcp_stream_decode_frame(frame, sizeof(frame), &state));
    frame[3] = 0;
    frame[15] = 1;
    assert(!tcp_stream_decode_frame(frame, sizeof(frame), &state));

    memset(&buffer, 0, sizeof(buffer));
    memset(&capture, 0, sizeof(capture));
    frame[15] = 0;
    assert(tcp_stream_feed(&buffer, frame, 7, capture_frame, &capture));
    assert(buffer.used == 7);
    /* A disconnect with used != 0 is discarded by the connection owner. */
    assert(capture.calls == 0);
}

int main(void)
{
    test_valid_and_fragmented_frames();
    test_invalid_and_incomplete_frames();
    puts("TCP frame regression tests passed.");
    return 0;
}
