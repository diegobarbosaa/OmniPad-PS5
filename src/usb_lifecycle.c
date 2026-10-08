#include "usb_lifecycle.h"

#include <errno.h>
#include <string.h>

static int valid_ops(const usb_lifecycle_ops_t *ops)
{
    return ops && ops->setup_io && ops->reserve_slot && ops->create_thread &&
           ops->join_thread && ops->release_slot && ops->release_io;
}

int usb_lifecycle_start(usb_lifecycle_t *lifecycle,
                        const usb_lifecycle_ops_t *ops, void *context,
                        void *(*worker)(void *), void *argument)
{
    int error;
    if (!lifecycle || !valid_ops(ops) || !worker) return EINVAL;
    if (lifecycle->io_owned || lifecycle->slot_owned || lifecycle->thread_started) {
        return EALREADY;
    }

    /* Setup may fail after partially allocating resources; cleanup must be safe. */
    lifecycle->io_owned = 1;
    error = ops->setup_io(context);
    if (error != 0) goto rollback;

    error = ops->reserve_slot(context);
    if (error != 0) goto rollback;
    lifecycle->slot_owned = 1;

    error = ops->create_thread(context, &lifecycle->thread, NULL, worker, argument);
    if (error != 0) goto rollback;
    lifecycle->thread_started = 1;
    return 0;

rollback:
    if (lifecycle->slot_owned) {
        ops->release_slot(context);
        lifecycle->slot_owned = 0;
    }
    if (lifecycle->io_owned) {
        ops->release_io(context);
        lifecycle->io_owned = 0;
    }
    memset(&lifecycle->thread, 0, sizeof(lifecycle->thread));
    return error;
}

int usb_lifecycle_stop(usb_lifecycle_t *lifecycle,
                       const usb_lifecycle_ops_t *ops, void *context)
{
    int error;
    if (!lifecycle || !valid_ops(ops)) return EINVAL;

    if (lifecycle->thread_started) {
        error = ops->join_thread(context, lifecycle->thread, NULL);
        if (error != 0) return error;
        lifecycle->thread_started = 0;
    }
    if (lifecycle->slot_owned) {
        ops->release_slot(context);
        lifecycle->slot_owned = 0;
    }
    if (lifecycle->io_owned) {
        ops->release_io(context);
        lifecycle->io_owned = 0;
    }
    memset(&lifecycle->thread, 0, sizeof(lifecycle->thread));
    return 0;
}
