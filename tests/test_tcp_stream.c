#define _DEFAULT_SOURCE
#include "../src/tcp_stream.h"
#include "../src/ps5_vpad.h"
#include "../src/tcp_frames.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

static int occupied[MAX_SLOTS];
static int update_calls;
static int remove_calls;
static int last_updated_slot = -1;
static pad_state_t last_state;

void log_line(const char *format, ...)
{
    (void)format;
}

int vpad_add(int slot, pad_conn_type_t type, const char *name)
{
    (void)type; (void)name;
    if (slot < 0 || slot >= MAX_SLOTS || occupied[slot]) return 0;
    occupied[slot] = 1;
    return 1;
}

void vpad_remove(int slot)
{
    if (slot >= 0 && slot < MAX_SLOTS && occupied[slot]) {
        occupied[slot] = 0;
        remove_calls++;
    }
}

void vpad_update(int slot, const pad_state_t *state)
{
    update_calls++;
    last_updated_slot = slot;
    last_state = *state;
}

static int connect_client(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;
    assert(fd >= 0);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)tcp_stream_test_port());
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0);
    return fd;
}

static void poll_until_counts(int expected_updates, int expected_removes)
{
    for (int i = 0; i < 50 &&
         (update_calls < expected_updates || remove_calls < expected_removes); i++) {
        usleep(1000);
        tcp_stream_poll(0);
    }
}

static void test_fragmented_input_and_slot_ownership(void)
{
    uint8_t frame[TCP_STREAM_FRAME_SIZE] = {
        0x00, 0x40, 0, 0, 1, 2, 3, 4, 5, 6, 0, 0, 0, 0, 0, 0
    };
    int fd;
    int before_remove = remove_calls;

    occupied[0] = 1; /* Simulated physical controller already owns slot 0. */
    fd = connect_client();
    assert(send(fd, frame, 5, 0) == 5);
    tcp_stream_poll(0);
    assert(occupied[0]);
    assert(occupied[1]);
    assert(update_calls == 0);
    assert(send(fd, frame + 5, sizeof(frame) - 5u, 0) == (ssize_t)(sizeof(frame) - 5u));
    tcp_stream_poll(0);
    poll_until_counts(1, before_remove);
    assert(update_calls == 1);
    assert(last_updated_slot == 1);
    assert(last_state.buttons == PAD_BTN_CROSS);
    assert(last_state.lx == 1 && last_state.r2 == 6);
    close(fd);
    tcp_stream_poll(0);
    poll_until_counts(update_calls, before_remove + 1);
    assert(occupied[0]);
    assert(!occupied[1]);
    assert(remove_calls == before_remove + 1);
}

static void test_truncated_invalid_and_oversized_connections(void)
{
    uint8_t frame[TCP_STREAM_FRAME_SIZE] = {0};
    int before_update = update_calls;
    int before_remove = remove_calls;
    int fd;

    fd = connect_client();
    assert(send(fd, frame, 7, 0) == 7);
    close(fd);
    tcp_stream_poll(0);
    poll_until_counts(before_update, before_remove + 1);
    assert(update_calls == before_update);
    assert(remove_calls == before_remove + 1);

    fd = connect_client();
    frame[3] = 0x80; /* Reserved button bit. */
    assert(send(fd, frame, sizeof(frame), 0) == (ssize_t)sizeof(frame));
    close(fd);
    tcp_stream_poll(0);
    poll_until_counts(before_update, before_remove + 2);
    assert(update_calls == before_update);
    assert(remove_calls == before_remove + 2);

    fd = connect_client();
    frame[3] = 0;
    uint8_t oversized[sizeof(frame) + 1u];
    memcpy(oversized, frame, sizeof(frame));
    oversized[sizeof(frame)] = 1;
    assert(send(fd, oversized, sizeof(oversized), 0) == (ssize_t)sizeof(oversized));
    close(fd);
    tcp_stream_poll(0);
    poll_until_counts(before_update + 1, before_remove + 3);
    assert(update_calls == before_update + 1);
    assert(remove_calls == before_remove + 3);
}

static void test_empty_and_occupied_slots(void)
{
    int fd;
    int before_adds = update_calls;

    fd = connect_client();
    close(fd);
    tcp_stream_poll(0);
    poll_until_counts(update_calls, remove_calls + 1);
    assert(remove_calls >= 3);

    for (int i = 0; i < MAX_SLOTS; i++) occupied[i] = 1;
    fd = connect_client();
    uint8_t frame[TCP_STREAM_FRAME_SIZE] = {0};
    assert(send(fd, frame, sizeof(frame), 0) == (ssize_t)sizeof(frame));
    tcp_stream_poll(0);
    assert(update_calls == before_adds); /* no new input reaches occupied slots */
    close(fd);
    for (int i = 0; i < MAX_SLOTS; i++) occupied[i] = 0;
}

int main(void)
{
    assert(tcp_stream_init(39045));
    test_fragmented_input_and_slot_ownership();
    test_truncated_invalid_and_oversized_connections();
    test_empty_and_occupied_slots();
    tcp_stream_cleanup();
    puts("TCP debug stream lifecycle tests passed.");
    return 0;
}
