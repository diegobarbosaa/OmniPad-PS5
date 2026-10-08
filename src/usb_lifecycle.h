#ifndef USB_LIFECYCLE_H
#define USB_LIFECYCLE_H

#include <pthread.h>

typedef struct {
    pthread_t thread;
    int io_owned;
    int slot_owned;
    int thread_started;
} usb_lifecycle_t;

typedef struct {
    int (*setup_io)(void *context);
    int (*reserve_slot)(void *context);
    int (*create_thread)(void *context, pthread_t *thread,
                         const pthread_attr_t *attributes,
                         void *(*worker)(void *), void *argument);
    int (*join_thread)(void *context, pthread_t thread, void **result);
    void (*release_slot)(void *context);
    void (*release_io)(void *context);
} usb_lifecycle_ops_t;

/* Returns zero when started/stopped, otherwise a pthread-style error code. */
int usb_lifecycle_start(usb_lifecycle_t *lifecycle,
                        const usb_lifecycle_ops_t *ops, void *context,
                        void *(*worker)(void *), void *argument);
int usb_lifecycle_stop(usb_lifecycle_t *lifecycle,
                       const usb_lifecycle_ops_t *ops, void *context);

#endif
