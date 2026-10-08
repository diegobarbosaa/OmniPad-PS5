#include "usb_hotplug.h"
#include "usb_controllers.h"
#include "ps5_vpad.h"
#include "log.h"
#include "usb_lifecycle.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>

#ifdef __PROSPERO__
#include <dev/usb/usb.h>
#include <dev/usb/usb_ioctl.h>
#include <dev/usb/usb_endian.h>
#include <dev/usb/usbdi.h>
#endif

typedef struct {
    int                   active;
    int                   slot;
    int                   fd;        /* /dev/ugenX.Y control device */
    char                  dev_path[32];
    uint16_t              vid, pid;
    usb_controller_type_t type;
    const char           *name;
    pad_conn_type_t       conn_type;
    usb_lifecycle_t       lifecycle;
    int                   stop_thread;
    int                   use_fs;

#ifdef __PROSPERO__
    struct usb_fs_endpoint endpoints[4];
    uint8_t               in_index;
    uint8_t               out_index;
    int                   has_out;
    void                 *fs_frame[4];
    uint32_t              fs_len[4];
    uint8_t               rx_buf[64];
    uint8_t               tx_buf[64];
#else
    int                   ep_fd[6];
#endif
} usb_slot_device_t;

typedef struct {
    usb_slot_device_t devices[MAX_SLOTS];
    long              last_scan_time;
    int               initialized;
    int               shutting_down;
    pthread_mutex_t   lock;
} usb_hotplug_state_t;

static usb_hotplug_state_t g_usb_hotplug = {
    .lock = PTHREAD_MUTEX_INITIALIZER
};
static pthread_mutex_t g_usb_operation_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t s_last_seen[6][21] = {{0}};

static void reset_device(usb_slot_device_t *sdev)
{
    memset(sdev, 0, sizeof(*sdev));
    sdev->slot = -1;
    sdev->fd = -1;
#ifndef __PROSPERO__
    for (int i = 0; i < 6; i++) sdev->ep_fd[i] = -1;
#endif
}

static void close_device_resources(usb_slot_device_t *sdev)
{
#ifdef __PROSPERO__
    if (sdev->use_fs && sdev->fd >= 0) {
        struct usb_fs_uninit uninit;
        memset(&uninit, 0, sizeof(uninit));
        ioctl(sdev->fd, USB_FS_UNINIT, &uninit);
        sdev->use_fs = 0;
    }
#else
    for (int i = 0; i < 6; i++) {
        if (sdev->ep_fd[i] >= 0) {
            close(sdev->ep_fd[i]);
            sdev->ep_fd[i] = -1;
        }
    }
#endif
    if (sdev->fd >= 0) {
        close(sdev->fd);
        sdev->fd = -1;
    }
}

static int worker_stop_requested(usb_slot_device_t *dev)
{
    int stop;
    pthread_mutex_lock(&g_usb_hotplug.lock);
    stop = dev->stop_thread;
    pthread_mutex_unlock(&g_usb_hotplug.lock);
    return stop;
}

static void request_worker_stop(usb_slot_device_t *dev)
{
    pthread_mutex_lock(&g_usb_hotplug.lock);
    dev->stop_thread = 1;
    pthread_mutex_unlock(&g_usb_hotplug.lock);
}

#ifdef __PROSPERO__

static void find_usb_endpoints(int fd, usb_controller_type_t type, uint16_t vid, uint16_t pid, uint8_t *in_addr, uint8_t *out_addr)
{
    *in_addr = 0;
    *out_addr = 0;
    struct usb_gen_descriptor desc;
    uint8_t raw[512];
    memset(&desc, 0, sizeof(desc));
    memset(raw, 0, sizeof(raw));
    desc.ugd_data = raw;
    desc.ugd_maxlen = sizeof(raw);
    if (ioctl(fd, USB_GET_FULL_DESC, &desc) != 0) return;
    int len = desc.ugd_actlen;
    if (len <= 0 || len > (int)sizeof(raw)) len = (int)sizeof(raw);

    uint8_t ins[8], outs[8];
    int in_cnt = 0, out_cnt = 0;
    int offset = 0;
    while (offset + 2 <= len) {
        int entry = raw[offset];
        if (entry < 2 || offset + entry > len) break;
        uint8_t bDescriptorType = raw[offset + 1];
        if (bDescriptorType == 0x05 && entry >= 5) { /* Endpoint descriptor */
            uint8_t ep_addr = raw[offset + 2];
            uint8_t ep_attr = raw[offset + 3] & 0x03;
            if (ep_attr == 0x03 || ep_attr == 0x02) { /* Interrupt or Bulk */
                if ((ep_addr & 0x80) && in_cnt < 8) ins[in_cnt++] = ep_addr;
                else if (!(ep_addr & 0x80) && out_cnt < 8) outs[out_cnt++] = ep_addr;
            }
        }
        offset += entry;
    }

    int is_xbox_one = (type == CTRL_XBOX_XINPUT && vid == 0x045e && pid != 0x028e);

    if (is_xbox_one) {
        /* Only Xbox One / Series X|S (GIP) uses EP 0x82 IN and 0x02 OUT */
        for (int i = 0; i < in_cnt; i++) {
            if (ins[i] == 0x82) { *in_addr = 0x82; break; }
        }
        for (int i = 0; i < out_cnt; i++) {
            if (outs[i] == 0x02) { *out_addr = 0x02; break; }
        }
    } else {
        /* Xbox 360, Machenike G5 Pro, ShanWan, Switch, DS4, DS3 all use EP 0x81 IN */
        for (int i = 0; i < in_cnt; i++) {
            if (ins[i] == 0x81) { *in_addr = 0x81; break; }
        }
        for (int i = 0; i < out_cnt; i++) {
            if (outs[i] == 0x02) { *out_addr = 0x02; break; }
        }
        if (*out_addr == 0) {
            for (int i = 0; i < out_cnt; i++) {
                if (outs[i] == 0x01) { *out_addr = 0x01; break; }
            }
        }
    }

    if (*in_addr == 0 && in_cnt > 0) *in_addr = ins[0];
    if (*out_addr == 0 && out_cnt > 0) *out_addr = outs[0];
}

static int usb_fs_setup_device(usb_slot_device_t *sdev)
{
    int iface = 0;
    ioctl(sdev->fd, USB_CLAIM_INTERFACE, &iface);

    int config = 0;
    if (ioctl(sdev->fd, USB_GET_CONFIG, &config) == 0 && (config & 0xff) == 0xff) {
        config = 0;
        ioctl(sdev->fd, USB_SET_CONFIG, &config);
    }

    uint8_t in_addr = 0, out_addr = 0;
    find_usb_endpoints(sdev->fd, sdev->type, sdev->vid, sdev->pid, &in_addr, &out_addr);

    int is_xbox_one = (sdev->type == CTRL_XBOX_XINPUT && sdev->vid == 0x045e && sdev->pid != 0x028e);
    if (in_addr == 0) {
        in_addr = is_xbox_one ? 0x82 : 0x81;
    }
    if (out_addr == 0 && (sdev->type == CTRL_XBOX_XINPUT)) {
        out_addr = is_xbox_one ? 0x02 : 0x01;
    }

    struct usb_fs_init init;
    memset(sdev->endpoints, 0, sizeof(sdev->endpoints));
    memset(&init, 0, sizeof(init));
    init.pEndpoints = sdev->endpoints;
    init.ep_index_max = 2;

    if (ioctl(sdev->fd, USB_FS_INIT, &init) != 0) {
        if (errno != ENOTTY && errno != ENXIO && errno != EIO && errno != EBADF) {
            log_line("usb_hotplug: USB_FS_INIT failed for %s (%s)", sdev->dev_path, strerror(errno));
        }
        return 0;
    }
    sdev->use_fs = 1;

    sdev->in_index = 0;
    struct usb_fs_open open_in;
    int in_opened = 0;
    uint8_t try_ins[] = { in_addr, 0x81, 0x82, 0x83, 0x84 };
    for (size_t i = 0; i < sizeof(try_ins); i++) {
        if (try_ins[i] == 0) continue;
        memset(&open_in, 0, sizeof(open_in));
        open_in.max_bufsize = 64;
        open_in.max_frames = 1;
        open_in.ep_index = sdev->in_index;
        open_in.ep_no = try_ins[i];
        if (ioctl(sdev->fd, USB_FS_OPEN, &open_in) == 0) {
            in_addr = try_ins[i];
            in_opened = 1;
            break;
        }
    }
    if (!in_opened) {
        log_line("usb_hotplug: USB_FS_OPEN IN failed for %s (%s)", sdev->dev_path, strerror(errno));
        return 0;
    }

    sdev->has_out = 0;
    uint8_t try_outs[] = { out_addr, 0x02, 0x01, 0x03, 0x04 };
    for (size_t i = 0; i < sizeof(try_outs); i++) {
        if (try_outs[i] == 0) continue;
        sdev->out_index = 1;
        struct usb_fs_open open_out;
        memset(&open_out, 0, sizeof(open_out));
        open_out.max_bufsize = 64;
        open_out.max_frames = 1;
        open_out.ep_index = sdev->out_index;
        open_out.ep_no = try_outs[i];
        if (ioctl(sdev->fd, USB_FS_OPEN, &open_out) == 0) {
            out_addr = try_outs[i];
            sdev->has_out = 1;
            break;
        }
    }

    sdev->fs_frame[0] = sdev->rx_buf;
    sdev->fs_len[0] = sizeof(sdev->rx_buf);
    sdev->endpoints[0].ppBuffer = &sdev->fs_frame[0];
    sdev->endpoints[0].pLength = &sdev->fs_len[0];
    sdev->endpoints[0].nFrames = 1;
    sdev->endpoints[0].flags = USB_FS_FLAG_SINGLE_SHORT_OK;

    sdev->fs_frame[1] = sdev->tx_buf;
    sdev->fs_len[1] = sizeof(sdev->tx_buf);
    sdev->endpoints[1].ppBuffer = &sdev->fs_frame[1];
    sdev->endpoints[1].pLength = &sdev->fs_len[1];
    sdev->endpoints[1].nFrames = 1;
    sdev->endpoints[1].flags = USB_FS_FLAG_SINGLE_SHORT_OK;

    log_line("usb_hotplug: %s claimed via USB_FS (IN=0x%02x, OUT=0x%02x)",
             sdev->name, in_addr, out_addr);
    return 1;
}

static int usb_fs_recv_packet(usb_slot_device_t *dev, uint8_t *buffer, uint32_t length, int timeout_ms)
{
    if (!dev->use_fs) return -1;
    struct usb_fs_endpoint *ep = &dev->endpoints[dev->in_index];
    uint32_t ask_len = length > 64 ? 64 : length;
    dev->fs_len[dev->in_index] = ask_len;
    ep->timeout = (uint16_t)timeout_ms;

    struct usb_fs_start start;
    memset(&start, 0, sizeof(start));
    start.ep_index = dev->in_index;
    if (ioctl(dev->fd, USB_FS_START, &start) != 0) {
        if (errno != EBUSY) return -1;
    }

    int spins = timeout_ms / 4 + 1;
    while (spins-- > 0) {
        struct usb_fs_complete complete;
        memset(&complete, 0, sizeof(complete));
        complete.ep_index = dev->in_index;
        if (ioctl(dev->fd, USB_FS_COMPLETE, &complete) == 0) {
            if (ep->status == USB_ERR_NORMAL_COMPLETION || ep->status == USB_ERR_SHORT_XFER) {
                uint32_t n = dev->fs_len[dev->in_index];
                if (n > length) n = length;
                memcpy(buffer, dev->rx_buf, n);
                return (int)n;
            }
            return 0;
        }
        if (errno == EBUSY || errno == ETIMEDOUT || errno == EAGAIN || errno == EWOULDBLOCK) {
            /* Transfer still in progress or waiting for packet — continue select loop */
        } else {
            return -1;
        }

        struct timeval wait;
        fd_set rset, wset;
        FD_ZERO(&rset);
        FD_ZERO(&wset);
        FD_SET(dev->fd, &rset);
        FD_SET(dev->fd, &wset);
        wait.tv_sec = 0;
        wait.tv_usec = 4000;
        select(dev->fd + 1, &rset, &wset, NULL, &wait);
    }
    return 0;
}

static int usb_fs_send_packet(usb_slot_device_t *dev, const uint8_t *buffer, uint32_t length, int timeout_ms)
{
    if (!dev->use_fs || !dev->has_out) return -1;
    struct usb_fs_endpoint *ep = &dev->endpoints[dev->out_index];
    uint32_t send_len = length > 64 ? 64 : length;
    memcpy(dev->tx_buf, buffer, send_len);
    dev->fs_len[dev->out_index] = send_len;
    ep->timeout = (uint16_t)timeout_ms;

    struct usb_fs_start start;
    memset(&start, 0, sizeof(start));
    start.ep_index = dev->out_index;
    if (ioctl(dev->fd, USB_FS_START, &start) != 0) {
        if (errno != EBUSY) return -1;
    }

    int spins = timeout_ms / 5 + 1;
    while (spins-- > 0) {
        struct usb_fs_complete complete;
        memset(&complete, 0, sizeof(complete));
        complete.ep_index = dev->out_index;
        if (ioctl(dev->fd, USB_FS_COMPLETE, &complete) == 0) {
            return (ep->status == USB_ERR_NORMAL_COMPLETION || ep->status == USB_ERR_SHORT_XFER) ? (int)send_len : -1;
        }
        if (errno != EBUSY) return -1;
        usleep(4000);
    }
    return -1;
}

static void ack_gip_packet(usb_slot_device_t *dev, const uint8_t *report, int count)
{
    if (!dev->has_out || !report || count < 4) return;
    size_t offset = 0;
    while (offset + 4 <= (size_t)count) {
        const uint8_t *msg = report + offset;
        size_t payload = msg[3];
        if ((msg[3] & 0x80) || payload < 2 || payload > 60 || offset + 4 + payload > (size_t)count) break;
        offset += 4 + payload;
        if ((msg[1] & 0x10) == 0) continue;
        uint8_t client = (uint8_t)((msg[1] & 0x0f) | 0x20);
        uint8_t ack[13];
        memset(ack, 0, sizeof(ack));
        ack[0] = 0x01;
        ack[1] = client;
        ack[2] = msg[2];
        ack[3] = 0x09;
        ack[5] = msg[0];
        ack[6] = client;
        ack[7] = (uint8_t)payload;
        usb_fs_send_packet(dev, ack, sizeof(ack), 40);
    }
}

#endif /* __PROSPERO__ */

static void *usb_reader_worker(void *arg)
{
    usb_slot_device_t *dev = (usb_slot_device_t *)arg;
    uint8_t report[256];
    pad_state_t st;
    
    long last_data_time = now_ms();
#ifdef __PROSPERO__
    long last_led_time = 0;
#endif
    long last_log_t = 0;
    int consecutive_errors = 0;

    log_line("usb_hotplug: Reader thread started for slot %d (%s on %s)",
             dev->slot, dev->name, dev->dev_path);

#ifdef __PROSPERO__
    int is_xbox_one = (dev->type == CTRL_XBOX_XINPUT && dev->vid == 0x045e && dev->pid != 0x028e);
    int is_genuine_x360 = (dev->type == CTRL_XBOX_XINPUT && dev->vid == 0x045e && dev->pid == 0x028e);
    if (dev->use_fs && dev->has_out && is_xbox_one) {
        static const uint8_t power_on[] = {0x05, 0x20, 0x00, 0x01, 0x00};
        static const uint8_t s_init[] = {
            0x05, 0x20, 0x00, 0x0f, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x55, 0x53, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        static const uint8_t led_on[] = {0x0a, 0x20, 0x00, 0x03, 0x00, 0x01, 0x14};
        usb_fs_send_packet(dev, power_on, sizeof(power_on), 80);
        usleep(10000);
        usb_fs_send_packet(dev, s_init, sizeof(s_init), 80);
        usleep(10000);
        usb_fs_send_packet(dev, led_on, sizeof(led_on), 80);
        log_line("usb_hotplug: GIP power/s_init/led sent on slot %d", dev->slot);
    }
#endif

    while (!worker_stop_requested(dev)) {
        long cur_time = now_ms();

#ifdef __PROSPERO__
        /* Send periodic Player 1 LED / keepalive ONLY for genuine Microsoft Xbox 360 controller */
        if (dev->use_fs && dev->has_out && is_genuine_x360 && (cur_time - last_led_time > 1500)) {
            last_led_time = cur_time;
            uint8_t x360_led[] = { 0x01, 0x03, 0x06 };
            usb_fs_send_packet(dev, x360_led, sizeof(x360_led), 40);
        }
#endif

        int n = 0;
#ifdef __PROSPERO__
        if (dev->use_fs) {
            n = usb_fs_recv_packet(dev, report, sizeof(report), 15);
            if (n > 0 && is_xbox_one) {
                ack_gip_packet(dev, report, n);
            }
        }
#else
        for (int i = 0; i < 6; i++) {
            if (dev->ep_fd[i] >= 0) {
                ssize_t rb = read(dev->ep_fd[i], report, sizeof(report));
                if (rb > 0) { n = (int)rb; break; }
            }
        }
        if (n <= 0 && dev->fd >= 0) {
            ssize_t rb = read(dev->fd, report, sizeof(report));
            if (rb > 0) n = (int)rb;
        }
#endif

        if (n > 0) {
            consecutive_errors = 0;
            int parsed = usb_parse_input_report(dev->type, report, (size_t)n, &st);
            if (!parsed) {
                parsed = usb_parse_input_report(CTRL_GENERIC_HID, report, (size_t)n, &st);
            }
            if (parsed) {
                st.conn_type = dev->conn_type;
                st.vid = dev->vid;
                st.pid = dev->pid;
                snprintf(st.controller_name, sizeof(st.controller_name), "%s", dev->name);
                /* If 2.4G dongle was in standby / slot freed, re-arm the virtual pad */
                if (dev->conn_type == CONN_USB_DONGLE_24G && vpad_slot_is_free(dev->slot)) {
                    log_line("usb_hotplug: slot %d 2.4G controller active — re-arming virtual pad", dev->slot);
                    vpad_add(dev->slot, dev->conn_type, dev->name);
                }
                vpad_update(dev->slot, &st);
                last_data_time = cur_time;

                if (st.buttons != 0 && (cur_time - last_log_t > 300)) {
                    last_log_t = cur_time;
                    log_line("usb_hotplug: slot %d button press 0x%08x (LX=%d LY=%d)",
                             dev->slot, (unsigned)st.buttons, st.lx, st.ly);
                }
            } else if (cur_time - last_log_t > 2000) {
                last_log_t = cur_time;
                log_line("usb_hotplug: slot %d unparsed %d bytes: %02x %02x %02x %02x %02x %02x",
                         dev->slot, n, report[0], report[1], report[2], report[3], report[4], report[5]);
            }
        } else if (n < 0) {
            consecutive_errors++;
            if (consecutive_errors >= 50) {
                log_line("usb_hotplug: slot %d disconnected (read error n=%d, errs=%d) — terminating reader",
                         dev->slot, n, consecutive_errors);
                request_worker_stop(dev);
                break;
            }
        } else if (cur_time - last_data_time > 3000 && cur_time - last_log_t > 3000) {
            last_log_t = cur_time;
            log_line("usb_hotplug: slot %d idle/standby (n=%d, use_fs=%d)",
                     dev->slot, n, dev->use_fs);
            /* If 2.4G dongle receives no packets for > 60 seconds (1 minute), release vpad to free user profile */
            if (dev->conn_type == CONN_USB_DONGLE_24G && (cur_time - last_data_time > 60000)) {
                if (!vpad_slot_is_free(dev->slot)) {
                    log_line("usb_hotplug: slot %d 2.4G dongle idle for 60s — releasing virtual pad to standby", dev->slot);
                    vpad_remove(dev->slot);
                }
            }
        }

        usleep(4000); /* ~250 Hz polling tick */
    }
    log_line("usb_hotplug: Reader thread stopped for slot %d", dev->slot);
    return NULL;
}

static int usb_lifecycle_setup_io(void *context)
{
    usb_slot_device_t *sdev = (usb_slot_device_t *)context;
#ifdef __PROSPERO__
    return usb_fs_setup_device(sdev) ? 0 : EIO;
#else
    for (int ep = 1; ep <= 6; ep++) {
        char ep_path[40];
        snprintf(ep_path, sizeof(ep_path), "%s.%d", sdev->dev_path, ep);
        sdev->ep_fd[ep - 1] = open(ep_path, O_RDONLY | O_NONBLOCK);
    }
    return 0;
#endif
}

static int usb_lifecycle_reserve_slot(void *context)
{
    usb_slot_device_t *sdev = (usb_slot_device_t *)context;
    return vpad_add(sdev->slot, sdev->conn_type, sdev->name) ? 0 : ENOSPC;
}

static int usb_lifecycle_create_thread(void *context, pthread_t *thread,
                                       const pthread_attr_t *attributes,
                                       void *(*worker)(void *), void *argument)
{
    (void)context;
    return pthread_create(thread, attributes, worker, argument);
}

static int usb_lifecycle_join_thread(void *context, pthread_t thread,
                                     void **result)
{
    (void)context;
    return pthread_join(thread, result);
}

static void usb_lifecycle_release_slot(void *context)
{
    usb_slot_device_t *sdev = (usb_slot_device_t *)context;
    vpad_remove(sdev->slot);
}

static void usb_lifecycle_release_io(void *context)
{
    close_device_resources((usb_slot_device_t *)context);
}

static const usb_lifecycle_ops_t USB_LIFECYCLE_OPS = {
    usb_lifecycle_setup_io,
    usb_lifecycle_reserve_slot,
    usb_lifecycle_create_thread,
    usb_lifecycle_join_thread,
    usb_lifecycle_release_slot,
    usb_lifecycle_release_io
};

static int is_device_claimed(const char *path)
{
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (g_usb_hotplug.devices[i].active &&
            strcmp(g_usb_hotplug.devices[i].dev_path, path) == 0) {
            return 1;
        }
    }
    return 0;
}

static int find_free_slot(void)
{
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (!g_usb_hotplug.devices[i].active && vpad_slot_is_free(i)) {
            return i;
        }
    }
    return -1;
}

static usb_controller_type_t usb_inspect_descriptors(int fd, const char **name_out)
{
#ifdef __PROSPERO__
    struct usb_gen_descriptor desc;
    uint8_t raw[512];
    memset(&desc, 0, sizeof(desc));
    memset(raw, 0, sizeof(raw));
    desc.ugd_data = raw;
    desc.ugd_maxlen = sizeof(raw);
    if (ioctl(fd, USB_GET_FULL_DESC, &desc) != 0) return CTRL_UNKNOWN;
    int len = desc.ugd_actlen;
    if (len <= 0 || len > (int)sizeof(raw)) len = (int)sizeof(raw);

    int offset = 0;
    while (offset + 2 <= len) {
        int entry = raw[offset];
        if (entry < 2 || offset + entry > len) break;
        uint8_t bDescriptorType = raw[offset + 1];
        if (bDescriptorType == 0x04 && entry >= 9) { /* Interface descriptor */
            uint8_t if_class = raw[offset + 5];
            uint8_t if_subclass = raw[offset + 6];

            /* Xbox 360 / XInput interface (Class 0xFF, SubClass 0x5D) */
            if (if_class == 0xFF && if_subclass == 0x5D) {
                if (name_out) *name_out = "XInput Compatible Controller";
                return CTRL_XBOX_XINPUT;
            }
        }
        offset += entry;
    }
#else
    (void)fd; (void)name_out;
#endif
    return CTRL_UNKNOWN;
}

static void probe_usb_devices(void)
{
    char path[32];

    for (int bus = 0; bus <= 5; bus++) {
        for (int dev = 1; dev <= 20; dev++) {
            snprintf(path, sizeof(path), "/dev/ugen%d.%d", bus, dev);

            if (is_device_claimed(path)) continue;

            int fd = open(path, O_RDWR | O_NONBLOCK);
            if (fd < 0) continue;

            uint16_t vid = 0, pid = 0;
#ifdef __PROSPERO__
            struct usb_device_descriptor dd;
            if (ioctl(fd, USB_GET_DEVICE_DESC, &dd) == 0) {
                vid = UGETW(dd.idVendor);
                pid = UGETW(dd.idProduct);

                /* Explicitly ignore non-gamepad classes:
                 * 0x08 = Mass Storage, 0x09 = Hub, 0x02 = CDC, 0x01 = Audio, 0x0e = Video, 0xE0 = Bluetooth */
                if (dd.bDeviceClass == 0x08 || dd.bDeviceClass == 0x09 ||
                    dd.bDeviceClass == 0x02 || dd.bDeviceClass == 0x01 ||
                    dd.bDeviceClass == 0x0e || dd.bDeviceClass == 0xE0) {
                    close(fd);
                    continue;
                }
            }
#endif
            if (vid == 0 && pid == 0) {
                close(fd);
                continue;
            }

            const char *ctrl_name = NULL;
            usb_controller_type_t ctype = usb_identify_controller(vid, pid, &ctrl_name);

            /* If not found by VID/PID, inspect raw USB descriptors for XInput or HID classes */
            if (ctype == CTRL_UNKNOWN) {
                ctype = usb_inspect_descriptors(fd, &ctrl_name);
            }

            if (vid != 0 || pid != 0) {
                uint32_t packed = ((uint32_t)vid << 16) | pid;
                if (s_last_seen[bus][dev] != packed) {
                    s_last_seen[bus][dev] = packed;
                    log_line("usb_hotplug: Device discovered on %s (VID 0x%04x, PID 0x%04x, type %d: %s)",
                             path, vid, pid, (int)ctype, ctrl_name ? ctrl_name : "unrecognized");
                }
            }

            /* Skip non-gamepad / unrecognized devices (flash drives, keyboards, SSDs) */
            if (ctype == CTRL_UNKNOWN) {
                close(fd);
                continue;
            }

            int slot = find_free_slot();
            if (slot < 0) {
                log_line("usb_hotplug: No free vpad slot for %s (%04x:%04x)", ctrl_name, vid, pid);
                close(fd);
                return;
            }

            log_line("usb_hotplug: Claiming %s on %s (slot %d)", ctrl_name, path, slot);

            /* Perform controller-specific wake-up / LED handshake */
            usb_init_controller_handshake(fd, ctype);

            usb_slot_device_t *sdev = &g_usb_hotplug.devices[slot];
            reset_device(sdev);
            sdev->active = 1;
            sdev->slot = slot;
            sdev->fd = fd;
            snprintf(sdev->dev_path, sizeof(sdev->dev_path), "%s", path);
            sdev->vid = vid;
            sdev->pid = pid;
            sdev->type = ctype;
            sdev->name = ctrl_name;
            sdev->stop_thread = 0;

            int is_dongle = (vid == 0x045e && pid == 0x0719) ||
                            (vid == 0x045e && (pid == 0x02e6 || pid == 0x02fe)) ||
                            (vid == 0x1a34) ||
                            (vid == 0x2345 && pid == 0xe02f) ||
                            (vid == 0x2dc8) ||
                            (ctrl_name && (strstr(ctrl_name, "Dongle") != NULL ||
                                           strstr(ctrl_name, "Adapter") != NULL ||
                                           strstr(ctrl_name, "Receiver") != NULL));
            sdev->conn_type = is_dongle ? CONN_USB_DONGLE_24G : CONN_USB_WIRED;

            int thread_error = usb_lifecycle_start(
                &sdev->lifecycle, &USB_LIFECYCLE_OPS, sdev,
                usb_reader_worker, sdev);
            if (thread_error != 0) {
                log_line("usb_hotplug: Device lifecycle startup failed for %s (error=%d)",
                         path, thread_error);
                reset_device(sdev);
                continue;
            }
        }
    }
}

int usb_hotplug_init(void)
{
    pthread_mutex_lock(&g_usb_operation_lock);
    pthread_mutex_lock(&g_usb_hotplug.lock);
    if (g_usb_hotplug.initialized && g_usb_hotplug.shutting_down) {
        pthread_mutex_unlock(&g_usb_hotplug.lock);
        pthread_mutex_unlock(&g_usb_operation_lock);
        return 0;
    }
    if (!g_usb_hotplug.initialized) {
        for (int i = 0; i < MAX_SLOTS; i++) reset_device(&g_usb_hotplug.devices[i]);
        memset(s_last_seen, 0, sizeof(s_last_seen));
        g_usb_hotplug.last_scan_time = 0;
        g_usb_hotplug.initialized = 1;
    }
    g_usb_hotplug.shutting_down = 0;
    pthread_mutex_unlock(&g_usb_hotplug.lock);
    pthread_mutex_unlock(&g_usb_operation_lock);
    log_line("usb_hotplug: Initialized USB hotplug manager");
    return 1;
}

void usb_hotplug_poll(long now)
{
    int dead[MAX_SLOTS];
    int n_dead = 0;

    pthread_mutex_lock(&g_usb_operation_lock);
    pthread_mutex_lock(&g_usb_hotplug.lock);
    if (!g_usb_hotplug.initialized || g_usb_hotplug.shutting_down ||
        now - g_usb_hotplug.last_scan_time < 1500) {
        pthread_mutex_unlock(&g_usb_hotplug.lock);
        pthread_mutex_unlock(&g_usb_operation_lock);
        return;
    }
    g_usb_hotplug.last_scan_time = now;

    for (int i = 0; i < MAX_SLOTS; i++) {
        if (g_usb_hotplug.devices[i].active &&
            (access(g_usb_hotplug.devices[i].dev_path, F_OK) != 0 ||
             g_usb_hotplug.devices[i].stop_thread)) {
            log_line("usb_hotplug: Controller disconnected from %s (slot %d)",
                     g_usb_hotplug.devices[i].dev_path, i);
            g_usb_hotplug.devices[i].stop_thread = 1;
            dead[n_dead++] = i;
        }
    }
    pthread_mutex_unlock(&g_usb_hotplug.lock);

    /* Joins happen without the mutex the workers use to observe stop requests. */
    for (int d = 0; d < n_dead; d++) {
        int i = dead[d];
        usb_slot_device_t *sdev = &g_usb_hotplug.devices[i];
        int stop_error = usb_lifecycle_stop(&sdev->lifecycle,
                                            &USB_LIFECYCLE_OPS, sdev);
        if (stop_error != 0) {
            log_line("usb_hotplug: Device cleanup failed for slot %d (error=%d)",
                     i, stop_error);
            continue;
        }

        pthread_mutex_lock(&g_usb_hotplug.lock);
        int bus = 0, dev_idx = 0;
        if (sscanf(sdev->dev_path, "/dev/ugen%d.%d", &bus, &dev_idx) == 2 &&
            bus >= 0 && bus < 6 && dev_idx >= 0 && dev_idx < 21) {
            s_last_seen[bus][dev_idx] = 0;
        }
        reset_device(sdev);
        pthread_mutex_unlock(&g_usb_hotplug.lock);
    }

    pthread_mutex_lock(&g_usb_hotplug.lock);
    if (g_usb_hotplug.initialized && !g_usb_hotplug.shutting_down) {
        probe_usb_devices();
    }
    pthread_mutex_unlock(&g_usb_hotplug.lock);
    pthread_mutex_unlock(&g_usb_operation_lock);
}

void usb_hotplug_cleanup(void)
{
    pthread_mutex_lock(&g_usb_operation_lock);
    pthread_mutex_lock(&g_usb_hotplug.lock);
    if (!g_usb_hotplug.initialized) {
        pthread_mutex_unlock(&g_usb_hotplug.lock);
        pthread_mutex_unlock(&g_usb_operation_lock);
        return;
    }
    g_usb_hotplug.shutting_down = 1;
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (g_usb_hotplug.devices[i].active) {
            g_usb_hotplug.devices[i].stop_thread = 1;
        }
    }
    pthread_mutex_unlock(&g_usb_hotplug.lock);

    for (int i = 0; i < MAX_SLOTS; i++) {
        usb_slot_device_t *sdev = &g_usb_hotplug.devices[i];
        if (!sdev->active) continue;
        int stop_error = usb_lifecycle_stop(&sdev->lifecycle,
                                            &USB_LIFECYCLE_OPS, sdev);
        if (stop_error != 0) {
            log_line("usb_hotplug: Cleanup failed for slot %d (error=%d)",
                     i, stop_error);
            continue;
        }
        pthread_mutex_lock(&g_usb_hotplug.lock);
        reset_device(sdev);
        pthread_mutex_unlock(&g_usb_hotplug.lock);
    }

    pthread_mutex_lock(&g_usb_hotplug.lock);
    int still_active = 0;
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (g_usb_hotplug.devices[i].active) still_active = 1;
    }
    if (!still_active) {
        g_usb_hotplug.initialized = 0;
        g_usb_hotplug.shutting_down = 0;
        g_usb_hotplug.last_scan_time = 0;
    }
    pthread_mutex_unlock(&g_usb_hotplug.lock);
    pthread_mutex_unlock(&g_usb_operation_lock);
}
