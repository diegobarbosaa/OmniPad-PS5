#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>

typedef struct PadTouch {
	uint16_t x;
	uint16_t y;
	uint8_t id;
	uint8_t reserved[3];
} PadTouch;

typedef struct PadData {
	uint32_t buttons;
	uint8_t left_x;
	uint8_t left_y;
	uint8_t right_x;
	uint8_t right_y;
	uint8_t l2;
	uint8_t r2;
	uint8_t analog_reserved[2];
	float orient_x;
	float orient_y;
	float orient_z;
	float orient_w;
	float accel_x;
	float accel_y;
	float accel_z;
	float angvel_x;
	float angvel_y;
	float angvel_z;
	uint8_t touch_count;
	uint8_t touch_reserved[7];
	PadTouch touch[2];
	int32_t connected;
	uint64_t timestamp_us;
	uint32_t extension_unit_id;
	uint8_t extension_reserved;
	uint8_t extension_length;
	uint8_t extension_data[10];
	uint8_t connected_count;
	uint8_t reserved[2];
	uint8_t device_unique_length;
	uint8_t device_unique_data[12];
} PadData;

int main(void) {
    printf("sizeof(PadTouch) = %zu\n", sizeof(PadTouch));
    printf("sizeof(PadData) = %zu\n", sizeof(PadData));
    printf("offsetof buttons = 0x%02zx (%zu)\n", offsetof(PadData, buttons), offsetof(PadData, buttons));
    printf("offsetof left_x = 0x%02zx (%zu)\n", offsetof(PadData, left_x), offsetof(PadData, left_x));
    printf("offsetof analog_reserved = 0x%02zx (%zu)\n", offsetof(PadData, analog_reserved), offsetof(PadData, analog_reserved));
    printf("offsetof orient_x = 0x%02zx (%zu)\n", offsetof(PadData, orient_x), offsetof(PadData, orient_x));
    printf("offsetof accel_x = 0x%02zx (%zu)\n", offsetof(PadData, accel_x), offsetof(PadData, accel_x));
    printf("offsetof angvel_x = 0x%02zx (%zu)\n", offsetof(PadData, angvel_x), offsetof(PadData, angvel_x));
    printf("offsetof touch_count = 0x%02zx (%zu)\n", offsetof(PadData, touch_count), offsetof(PadData, touch_count));
    printf("offsetof touch = 0x%02zx (%zu)\n", offsetof(PadData, touch), offsetof(PadData, touch));
    printf("offsetof connected = 0x%02zx (%zu)\n", offsetof(PadData, connected), offsetof(PadData, connected));
    printf("offsetof timestamp_us = 0x%02zx (%zu)\n", offsetof(PadData, timestamp_us), offsetof(PadData, timestamp_us));
    printf("offsetof extension_unit_id = 0x%02zx (%zu)\n", offsetof(PadData, extension_unit_id), offsetof(PadData, extension_unit_id));
    printf("offsetof connected_count = 0x%02zx (%zu)\n", offsetof(PadData, connected_count), offsetof(PadData, connected_count));
    printf("offsetof device_unique_data = 0x%02zx (%zu)\n", offsetof(PadData, device_unique_data), offsetof(PadData, device_unique_data));

    assert(sizeof(PadTouch) == 8);
    assert(sizeof(PadData) == 120);
    assert(offsetof(PadData, buttons) == 0x00);
    assert(offsetof(PadData, touch_count) == 0x34);
    assert(offsetof(PadData, touch) == 0x3C);
    assert(offsetof(PadData, connected) == 0x4C);
    assert(offsetof(PadData, timestamp_us) == 0x50);
    assert(offsetof(PadData, connected_count) == 0x68);
    assert(offsetof(PadData, device_unique_data) == 0x6C);

    printf("ALL ASSERTS PASSED! 120-byte ABI verified.\n");
    return 0;
}
