/* Host integration: run the real vpad update/poll/removal with a controlled
 * clock and counted ShellUI call, without executing console code. */
#include <assert.h>
#include <stdio.h>
#define now_ms test_now_ms
#define shellui_press_ps_button test_shellui_press_ps_button
#include "../src/ps5_vpad.c"
#undef now_ms
#undef shellui_press_ps_button
#include "../src/ps_chord_hold.h"
/* Exercise the real USB worker on the host with a change-only receiver.
 * Only read(), sleep and console calls are simulated. */
static ssize_t test_read(int fd, void *buf, size_t len);
static int test_usleep(useconds_t us);
#define read test_read
#define usleep test_usleep
#define now_ms test_now_ms
#include "../src/usb_hotplug.c"
#undef read
#undef usleep
#undef now_ms
static long clock_ms = 1000;
static int clicks;
long test_now_ms(void) { return clock_ms; }
int test_shellui_press_ps_button(int32_t handle) { clicks++; return handle; }
/* Console-only dependencies are stubs; no assertion below claims to test
 * actual ShellUI, MBus, profile selection or physical hardware. */
int elevate_privileges(void) { return 0; }
void restore_privileges(void) {}
int shellcore_vda(int *code) { *code = 0; return -1; }
int shellui_remote_bind_device(uint64_t id, int32_t user) { (void)id; (void)user; return 0; }
int shellui_remote_unbind_device(uint64_t id, int32_t user) { (void)id; (void)user; return 0; }
int shellui_remote_disconnect_device(uint64_t id) { (void)id; return 0; }
static void ready_slot(int slot, const char *name) {
    memset(&g_slots[slot], 0, sizeof(g_slots[slot]));
    g_slots[slot].status = VP_READY;
    g_slots[slot].handle = slot + 1;
    g_slots[slot].stale_timeout_ms = OMNI_DEFAULT_STALE_TIMEOUT_MS;
    snprintf(g_slots[slot].name, sizeof(g_slots[slot].name), "%s", name);
}
static usb_slot_device_t *worker_device;
static int worker_report, ps_edges_down, ps_edges_up, previous_ps;
static ssize_t test_read(int fd, void *buf, size_t len) {
    (void)fd;
    int due = worker_report == 0 ||
              (worker_report == 1 && clock_ms >= 1700) ||
              (worker_report == 2 && clock_ms >= 2300);
    if (!due) return 0;
    assert(len >= 9);
    uint8_t r[9] = {0, 0x0c, 8, 64, 128, 128, 128, 0, 235};
    if (worker_report >= 1) { r[3] = 40; r[8] = 255; }
    if (worker_report == 2) r[1] = 0x04; /* Release Menu only. */
    memcpy(buf, r, sizeof(r));
    worker_report++;
    return sizeof(r);
}
static int test_usleep(useconds_t us) {
    assert(worker_device && us == 4000);
    clock_ms += 4;
    vpad_poll(clock_ms);
    int ps = !!(g_slots[0].pad_state.buttons & PAD_BTN_PS);
    if (ps && !previous_ps) ps_edges_down++;
    if (!ps && previous_ps) ps_edges_up++;
    previous_ps = ps;
    if (clock_ms < 1604) assert(!ps);
    else if (clock_ms < 2304) assert(ps);
    else assert(!ps);
    if (clock_ms >= 1704) {
        assert(g_slots[0].pad_state.lx == 40);
        assert(g_slots[0].pad_state.r2 == 255);
    }
    if (clock_ms < 2304)
        assert(!(g_slots[0].pad_state.buttons & (PAD_BTN_SHARE | PAD_BTN_OPTIONS)));
    if (clock_ms >= 2400) worker_device->stop_thread = 1;
    return 0;
}
static void test_real_worker(void) {
    usb_slot_device_t device = {0};
    ready_slot(0, "GameSir G7 Pro (USB HID)");
    g_slots[0].stale_timeout_ms = OMNI_EVENT_STALE_TIMEOUT_MS;
    clock_ms = 1000;
    device.slot = 0; device.type = CTRL_GAMESIR_G7PRO;
    device.name = "GameSir G7 Pro (USB HID)";
    device.vid = 0x3537; device.pid = 0x1022;
    device.stale_timeout_ms = usb_controller_stale_timeout_ms(device.type);
    device.fd = -1;
    for (int i = 0; i < 6; i++) device.ep_fd[i] = -1;
    device.ep_fd[0] = 10;
    worker_device = &device;
    int before = clicks;
    usb_reader_worker(&device);
    assert(worker_report == 3 && ps_edges_down == 1 && ps_edges_up == 1);
    assert(clicks == before); /* No independent ShellUI click. */
    assert(g_slots[0].pad_state.buttons & PAD_BTN_SHARE);
    vpad_remove(0);
    worker_device = NULL;
}
int main(void) {
    g_ready = 1;
    ready_slot(0, "GameSir G7 Pro (USB HID)");
    g_slots[0].stale_timeout_ms = OMNI_EVENT_STALE_TIMEOUT_MS;
    ready_slot(1, "Sony DualShock 4 (USB)");
    pad_state_t raw, out;
    pad_state_neutral(&raw);
    raw.buttons = PAD_BTN_SHARE | PAD_BTN_OPTIONS | PAD_BTN_R2;
    raw.r2 = 235; raw.lx = 40;
    omni_ps_chord_hold_t h = {0};
    omni_ps_hold_report(&h, &raw, &out, clock_ms, 20000);
    vpad_update_level(0, &out);
    clock_ms = 1600;
    assert(omni_ps_hold_tick(&h, &out, clock_ms, 20000));
    vpad_update_cached_level(0, &out);
    assert(clicks == 0);
    for (clock_ms = 1604; clock_ms <= 3600; clock_ms += 4) {
        vpad_poll(clock_ms);
        assert(g_slots[0].pad_state.buttons & PAD_BTN_PS);
        assert(g_slots[0].last_update_time == 1000);
        assert(g_slots[0].pad_state.lx == 40 && g_slots[0].pad_state.r2 == 235);
    }
    clock_ms = 21000; vpad_poll(clock_ms);
    assert(g_slots[0].pad_state.buttons & PAD_BTN_PS);
    clock_ms = 21001; vpad_poll(clock_ms);
    assert(g_slots[0].pad_state.buttons == 0);
    assert(!omni_ps_hold_tick(&h, &out, clock_ms, 20000) && h.blocked);
    /* Old entry point still sends a click exactly once on a rising edge. */
    pad_state_neutral(&out); out.buttons = PAD_BTN_PS | PAD_BTN_CROSS;
    vpad_update(1, &out); assert(clicks == 1);
    vpad_update(1, &out); assert(clicks == 1);
    clock_ms += 501; vpad_poll(clock_ms);
    assert(g_slots[1].pad_state.buttons != 0); /* Integration: default-bound USB pads are event-driven (#18). */
    out.buttons = 0; vpad_update(1, &out);
    out.buttons = PAD_BTN_PS; vpad_update(1, &out); assert(clicks == 2);
    vpad_remove(0); assert(g_slots[0].status == VP_FREE);
    vpad_update_level(0, &out); assert(g_slots[0].status == VP_FREE);
    assert(clicks == 2);
    test_real_worker();
    puts("Real USB worker + vpad integration, watchdog and legacy PS click contract passed.");
    return 0;
}
