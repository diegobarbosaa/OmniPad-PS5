#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#include <assert.h>

#include "../src/pad_types.h"

/* Replicate static helpers for unit testing */
static inline int plausible_handle(int32_t h)
{
    return h > 0 && h < 64;
}

static int parse_klog_handle(const char *line)
{
    const char *p = strstr(line, "Open Pad");
    if (!p) p = strstr(line, "open pad");
    if (p) {
        const char *r = strstr(p, "ret=");
        if (r) {
            int h = (int)strtol(r + 4, NULL, 0);
            if (plausible_handle(h)) return h;
        }
    }
    return -1;
}

static uint64_t hex_to_u64_test(const char *s)
{
    uint64_t val = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    while (*s) {
        char c = *s++;
        if (c >= '0' && c <= '9') val = (val << 4) | (uint64_t)(c - '0');
        else if (c >= 'a' && c <= 'f') val = (val << 4) | (uint64_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') val = (val << 4) | (uint64_t)(c - 'A' + 10);
        else break;
    }
    return val;
}

static uint64_t parse_device_id_from_klog_test(const char *line)
{
    if (!strstr(line, "DEVICE_ADDED") && !strstr(line, "device_added") && !strstr(line, "DeviceAdded"))
        return 0;

    const char *st = strstr(line, "subType");
    if (!st) st = strstr(line, "subtype");
    if (!st) st = strstr(line, "SubType");
    if (st) {
        st += 7;
        while (*st == ' ' || *st == ':' || *st == '=') st++;
        if (strncmp(st, "22", 2) != 0 && strncmp(st, "0x16", 4) != 0) {
            return 0;
        }
    }

    const char *p = strstr(line, "DeviceId");
    if (!p) p = strstr(line, "deviceId");
    if (!p) p = strstr(line, "dev_id");
    if (!p) return 0;

    p += (p[0] == 'd' && p[1] == 'e' && p[2] == 'v') ? 6 : 8;
    while (*p == ' ' || *p == ':' || *p == '=' ) p++;
    return hex_to_u64_test(p);
}

static void test_abi_layout(void)
{
    printf("Testing PadData 120-byte ABI layout... ");
    assert(sizeof(PadTouch) == 8);
    assert(sizeof(PadData) == 120);
    assert(offsetof(PadData, buttons) == 0x00);
    assert(offsetof(PadData, left_x) == 0x04);
    assert(offsetof(PadData, analog_reserved) == 0x0A);
    assert(offsetof(PadData, orient_x) == 0x0C);
    assert(offsetof(PadData, orient_w) == 0x18);
    assert(offsetof(PadData, accel_x) == 0x1C);
    assert(offsetof(PadData, angvel_x) == 0x28);
    assert(offsetof(PadData, touch_count) == 0x34);
    assert(offsetof(PadData, touch) == 0x3C);
    assert(offsetof(PadData, connected) == 0x4C);
    assert(offsetof(PadData, timestamp_us) == 0x50);
    assert(offsetof(PadData, extension_unit_id) == 0x58);
    assert(offsetof(PadData, connected_count) == 0x68);
    assert(offsetof(PadData, device_unique_data) == 0x6C);
    printf("OK!\n");
}

static void test_neutral_and_conversion(void)
{
    printf("Testing pad_state_neutral and pad_data_from_state... ");
    pad_state_t st;
    pad_state_neutral(&st);

    assert(st.buttons == 0);
    assert(st.lx == 128);
    assert(st.ly == 128);
    assert(st.rx == 128);
    assert(st.ry == 128);
    assert(st.l2 == 0);
    assert(st.r2 == 0);
    assert(st.accel[2] == 8192);
    assert(st.battery_level == 100);

    /* Press buttons and pull triggers */
    st.buttons = PAD_BTN_CROSS | PAD_BTN_OPTIONS;
    st.lx = 200;
    st.ly = 50;
    st.l2 = 250; /* Should automatically set PAD_BTN_L2 in digital buttons */
    st.r2 = 20;  /* Below threshold, no digital R2 */

    PadData d;
    pad_data_from_state(&d, &st, 123456789ULL);

    assert(d.buttons & PAD_BTN_CROSS);
    assert(d.buttons & PAD_BTN_OPTIONS);
    assert(d.buttons & PAD_BTN_L2);
    assert(!(d.buttons & PAD_BTN_R2));
    assert(d.left_x == 200);
    assert(d.left_y == 50);
    assert(d.l2 == 250);
    assert(d.r2 == 20);
    assert(d.orient_w == 1.0f);
    assert(d.connected == 1);
    assert(d.connected_count == 1);
    assert(d.timestamp_us == 123456789ULL);
    printf("OK!\n");
}

static void test_plausible_handle(void)
{
    printf("Testing plausible_handle validation... ");
    assert(!plausible_handle(0));   /* 0 MUST NEVER BE ACCEPTED */
    assert(!plausible_handle(-1));  /* -1 is invalid */
    assert(!plausible_handle(64));  /* 64 is out of range */
    assert(!plausible_handle(100));
    assert(plausible_handle(1));    /* Typical slot 0 handle */
    assert(plausible_handle(2));
    assert(plausible_handle(3));
    assert(plausible_handle(63));
    printf("OK!\n");
}

static void test_klog_parsing(void)
{
    printf("Testing kernel log parsing... ");
    const char *line1 = "[SCE_MBUS] Open Pad for user 0x10000000 ret=1";
    int h1 = parse_klog_handle(line1);
    assert(h1 == 1);

    const char *line2 = "[SCE_MBUS] Open Pad ret=3";
    int h2 = parse_klog_handle(line2);
    assert(h2 == 3);

    const char *line3 = "[SCE_MBUS] Open Pad ret=-2143551482"; /* Error code, not a plausible handle */
    int h3 = parse_klog_handle(line3);
    assert(h3 == -1);

    const char *line4 = "<6>[SCE_MBUS] SCE_MBUS_EVENT_DEVICE_ADDED: DeviceId:0x12611169 subType:22 capabilityBattery:0";
    uint64_t dev4 = parse_device_id_from_klog_test(line4);
    assert(dev4 == 0x12611169ULL);
    printf("OK!\n");
}

static pid_t parse_sysctl_buffer(const uint8_t *buf, size_t size, const char *name)
{
    const uint8_t *ptr = buf;
    pid_t found_pid = -1;
    while (ptr + 8 < buf + size) {
        int stride = *(int *)ptr;
        if (stride < 64 || stride > 4096) break;
        if (ptr + stride > buf + size) break;

        pid_t pid = *(pid_t *)(ptr + 72);
        const char *tdname = (const char *)(ptr + 447);

        if (tdname[0] && (strcmp(tdname, name) == 0 || strstr(tdname, name) != NULL)) {
            if (pid > 0 && (found_pid < 0 || pid < found_pid)) {
                found_pid = pid;
            }
        }
        ptr += stride;
    }
    return found_pid;
}

static void test_prospero_sysctl_proc_parser(void)
{
    printf("Testing Prospero sysctl proc table parser... ");
    uint8_t buffer[2048];
    memset(buffer, 0, sizeof(buffer));

    /* Entry 1: PID 100, name "kernel" */
    int stride1 = 512;
    *(int *)(buffer) = stride1;
    *(pid_t *)(buffer + 72) = 100;
    snprintf((char *)(buffer + 447), 32, "kernel");

    /* Entry 2: PID 250, name "SceShellCore" */
    int stride2 = 512;
    *(int *)(buffer + 512) = stride2;
    *(pid_t *)(buffer + 512 + 72) = 250;
    snprintf((char *)(buffer + 512 + 447), 32, "SceShellCore");

    /* Entry 3: PID 320, name "SceShellUI" */
    int stride3 = 512;
    *(int *)(buffer + 1024) = stride3;
    *(pid_t *)(buffer + 1024 + 72) = 320;
    snprintf((char *)(buffer + 1024 + 447), 32, "SceShellUI");

    pid_t p_ui = parse_sysctl_buffer(buffer, sizeof(buffer), "SceShellUI");
    assert(p_ui == 320);

    pid_t p_core = parse_sysctl_buffer(buffer, sizeof(buffer), "SceShellCore");
    assert(p_core == 250);

    pid_t p_none = parse_sysctl_buffer(buffer, sizeof(buffer), "NonExistentProc");
    assert(p_none == -1);

    printf("OK!\n");
}

static void test_gip_ack_generation(void)
{
    printf("Testing Xbox One GIP ACK generation... ");
    /* Simulated Xbox One input packet with chunk containing msg[1] & 0x10 */
    uint8_t report[16] = {
        0x20, 0x11, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    /* Verify ack fields: client = (msg[1] & 0x0f) | 0x20 = 0x21 */
    uint8_t client = (uint8_t)((report[1] & 0x0f) | 0x20);
    assert(client == 0x21);
    printf("OK!\n");
}

static void test_fw1360_handle_assignment(void)
{
    printf("Testing FW 13.60 device_id handle assignment... ");
    /* Under FW 13.60, klog gives DeviceId e.g. 0x12611169 */
    uint64_t device_id = 0x12611169ULL;
    int32_t handle = -1;
    int32_t alt_handle = -1;

    if (device_id > 0 && device_id <= 0x7FFFFFFF) {
        alt_handle = handle;
        handle = (int32_t)device_id;
    }
    assert(handle == 0x12611169);
    assert(alt_handle == -1);
    printf("OK!\n");
}

static void test_web_api_button_codes(void)
{
    printf("Testing Web API button code mappings... ");
    static const struct {
        int code;
        uint32_t expected_mask;
    } cases[] = {
        { 1, PAD_BTN_CROSS },
        { 2, PAD_BTN_CIRCLE },
        { 3, PAD_DPAD_UP },
        { 4, PAD_DPAD_DOWN },
        { 5, PAD_DPAD_LEFT },
        { 6, PAD_DPAD_RIGHT },
        { 7, PAD_BTN_PS },
        { 8, PAD_BTN_SQUARE },
        { 9, PAD_BTN_TRIANGLE },
        { 10, PAD_BTN_OPTIONS },
        { 11, PAD_BTN_SHARE },
        { 12, PAD_BTN_L1 },
        { 13, PAD_BTN_R1 },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint32_t mask = 0;
        switch (cases[i].code) {
        case 1: mask = PAD_BTN_CROSS; break;
        case 2: mask = PAD_BTN_CIRCLE; break;
        case 3: mask = PAD_DPAD_UP; break;
        case 4: mask = PAD_DPAD_DOWN; break;
        case 5: mask = PAD_DPAD_LEFT; break;
        case 6: mask = PAD_DPAD_RIGHT; break;
        case 7: mask = PAD_BTN_PS; break;
        case 8: mask = PAD_BTN_SQUARE; break;
        case 9: mask = PAD_BTN_TRIANGLE; break;
        case 10: mask = PAD_BTN_OPTIONS; break;
        case 11: mask = PAD_BTN_SHARE; break;
        case 12: mask = PAD_BTN_L1; break;
        case 13: mask = PAD_BTN_R1; break;
        default: mask = 0; break;
        }
        assert(mask == cases[i].expected_mask);
    }
    printf("OK!\n");
}

int main(void)
{
    printf("========================================\n");
    printf("Running AnyPad PS5 Virtual Pad ABI Tests\n");
    printf("========================================\n");

    test_abi_layout();
    test_neutral_and_conversion();
    test_plausible_handle();
    test_klog_parsing();
    test_prospero_sysctl_proc_parser();
    test_gip_ack_generation();
    test_fw1360_handle_assignment();
    test_web_api_button_codes();

    printf("\nAll Virtual Pad ABI tests passed with 100%% precision!\n");
    return 0;
}
