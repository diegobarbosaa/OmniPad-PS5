#include "../src/usb_lifecycle.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int setup_error;
    int reserve_error;
    int create_error;
    int join_error;
    int io_open;
    int slot_reserved;
    int setup_calls;
    int reserve_calls;
    int create_calls;
    int join_calls;
    int slot_release_calls;
    int io_release_calls;
} fake_device_t;

static int fake_setup(void *context)
{
    fake_device_t *device = (fake_device_t *)context;
    device->setup_calls++;
    device->io_open = 1; /* Include partial setup failure in rollback coverage. */
    return device->setup_error;
}

static int fake_reserve(void *context)
{
    fake_device_t *device = (fake_device_t *)context;
    device->reserve_calls++;
    if (device->reserve_error != 0) return device->reserve_error;
    device->slot_reserved = 1;
    return 0;
}

static int fake_create(void *context, pthread_t *thread,
                       const pthread_attr_t *attributes,
                       void *(*worker)(void *), void *argument)
{
    (void)thread; (void)attributes; (void)worker; (void)argument;
    fake_device_t *device = (fake_device_t *)context;
    device->create_calls++;
    return device->create_error;
}

static int fake_join(void *context, pthread_t thread, void **result)
{
    (void)thread; (void)result;
    fake_device_t *device = (fake_device_t *)context;
    device->join_calls++;
    return device->join_error;
}

static int real_create(void *context, pthread_t *thread,
                       const pthread_attr_t *attributes,
                       void *(*worker)(void *), void *argument)
{
    (void)context;
    return pthread_create(thread, attributes, worker, argument);
}

static int real_join(void *context, pthread_t thread, void **result)
{
    (void)context;
    return pthread_join(thread, result);
}

static void fake_release_slot(void *context)
{
    fake_device_t *device = (fake_device_t *)context;
    assert(device->slot_reserved);
    device->slot_reserved = 0;
    device->slot_release_calls++;
}

static void fake_release_io(void *context)
{
    fake_device_t *device = (fake_device_t *)context;
    device->io_open = 0;
    device->io_release_calls++;
}

static void *fake_worker(void *argument)
{
    return argument;
}

static usb_lifecycle_ops_t fake_ops = {
    fake_setup,
    fake_reserve,
    fake_create,
    fake_join,
    fake_release_slot,
    fake_release_io
};

static void test_setup_and_slot_failures_roll_back(void)
{
    usb_lifecycle_t lifecycle = {0};
    fake_device_t device = {.setup_error = EIO};
    assert(usb_lifecycle_start(&lifecycle, &fake_ops, &device,
                               fake_worker, &device) == EIO);
    assert(device.setup_calls == 1 && device.reserve_calls == 0);
    assert(!device.io_open && device.io_release_calls == 1);
    assert(!lifecycle.io_owned && !lifecycle.slot_owned && !lifecycle.thread_started);

    memset(&lifecycle, 0, sizeof(lifecycle));
    memset(&device, 0, sizeof(device));
    device.reserve_error = ENOSPC;
    assert(usb_lifecycle_start(&lifecycle, &fake_ops, &device,
                               fake_worker, &device) == ENOSPC);
    assert(device.setup_calls == 1 && device.reserve_calls == 1);
    assert(device.io_release_calls == 1 && device.slot_release_calls == 0);
    assert(!device.io_open && !device.slot_reserved);
}

static void test_thread_creation_failure_rolls_back_slot_and_io(void)
{
    usb_lifecycle_t lifecycle = {0};
    fake_device_t device = {.create_error = EAGAIN};
    assert(usb_lifecycle_start(&lifecycle, &fake_ops, &device,
                               fake_worker, &device) == EAGAIN);
    assert(device.setup_calls == 1 && device.reserve_calls == 1);
    assert(device.create_calls == 1 && device.slot_release_calls == 1);
    assert(device.io_release_calls == 1);
    assert(!device.io_open && !device.slot_reserved);
    assert(!lifecycle.io_owned && !lifecycle.slot_owned && !lifecycle.thread_started);
}

static void test_disconnect_immediately_and_repeat_cycles(void)
{
    usb_lifecycle_t lifecycle = {0};
    fake_device_t device = {0};
    assert(usb_lifecycle_start(&lifecycle, &fake_ops, &device,
                               fake_worker, &device) == 0);
    assert(device.io_open && device.slot_reserved && lifecycle.thread_started);
    assert(usb_lifecycle_stop(&lifecycle, &fake_ops, &device) == 0);
    assert(device.join_calls == 1 && device.slot_release_calls == 1);
    assert(device.io_release_calls == 1 && !device.io_open && !device.slot_reserved);
    assert(usb_lifecycle_stop(&lifecycle, &fake_ops, &device) == 0);
    assert(device.join_calls == 1 && device.slot_release_calls == 1);

    assert(usb_lifecycle_start(&lifecycle, &fake_ops, &device,
                               fake_worker, &device) == 0);
    assert(usb_lifecycle_stop(&lifecycle, &fake_ops, &device) == 0);
    assert(device.join_calls == 2 && device.slot_release_calls == 2);
    assert(device.io_release_calls == 2);
}

static void test_join_failure_retains_resources_for_retry(void)
{
    usb_lifecycle_t lifecycle = {0};
    fake_device_t device = {0};
    assert(usb_lifecycle_start(&lifecycle, &fake_ops, &device,
                               fake_worker, &device) == 0);
    device.join_error = EBUSY;
    assert(usb_lifecycle_stop(&lifecycle, &fake_ops, &device) == EBUSY);
    assert(lifecycle.thread_started && lifecycle.slot_owned && lifecycle.io_owned);
    assert(device.io_open && device.slot_reserved);
    assert(device.slot_release_calls == 0 && device.io_release_calls == 0);
    device.join_error = 0;
    assert(usb_lifecycle_stop(&lifecycle, &fake_ops, &device) == 0);
    assert(!lifecycle.thread_started && !lifecycle.slot_owned && !lifecycle.io_owned);
    assert(device.join_calls == 2 && device.slot_release_calls == 1);
    assert(device.io_release_calls == 1);
}

static void test_multiple_device_states_are_independent(void)
{
    usb_lifecycle_t first = {0}, second = {0};
    fake_device_t a = {0}, b = {0};
    assert(usb_lifecycle_start(&first, &fake_ops, &a, fake_worker, &a) == 0);
    assert(usb_lifecycle_start(&second, &fake_ops, &b, fake_worker, &b) == 0);
    assert(a.slot_reserved && b.slot_reserved);
    assert(usb_lifecycle_stop(&first, &fake_ops, &a) == 0);
    assert(!a.slot_reserved && b.slot_reserved);
    assert(usb_lifecycle_stop(&second, &fake_ops, &b) == 0);
    assert(!a.slot_reserved && !b.slot_reserved);
}

static void test_real_pthread_lifecycle(void)
{
    usb_lifecycle_t lifecycle = {0};
    fake_device_t device = {0};
    usb_lifecycle_ops_t real_ops = fake_ops;
    real_ops.create_thread = real_create;
    real_ops.join_thread = real_join;
    assert(usb_lifecycle_start(&lifecycle, &real_ops, &device,
                               fake_worker, &device) == 0);
    assert(lifecycle.thread_started);
    assert(usb_lifecycle_stop(&lifecycle, &real_ops, &device) == 0);
    assert(!lifecycle.thread_started && !device.io_open && !device.slot_reserved);
    assert(device.io_release_calls == 1 && device.slot_release_calls == 1);
}

int main(void)
{
    test_setup_and_slot_failures_roll_back();
    test_thread_creation_failure_rolls_back_slot_and_io();
    test_disconnect_immediately_and_repeat_cycles();
    test_join_failure_retains_resources_for_retry();
    test_multiple_device_states_are_independent();
    test_real_pthread_lifecycle();
    puts("USB lifecycle rollback and failure-injection tests passed.");
    return 0;
}
