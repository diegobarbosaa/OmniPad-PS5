#ifndef TCP_FRAMES_H
#define TCP_FRAMES_H

#include "pad_types.h"

#include <stddef.h>
#include <stdint.h>

#define TCP_STREAM_FRAME_SIZE 16u

typedef struct {
    uint8_t bytes[TCP_STREAM_FRAME_SIZE];
    size_t used;
} tcp_frame_buffer_t;

typedef int (*tcp_frame_callback_t)(const pad_state_t *state, void *context);

int tcp_stream_decode_frame(const uint8_t *frame, size_t length,
                            pad_state_t *state);
int tcp_stream_feed(tcp_frame_buffer_t *buffer, const uint8_t *data,
                    size_t length, tcp_frame_callback_t callback,
                    void *context);

#endif
