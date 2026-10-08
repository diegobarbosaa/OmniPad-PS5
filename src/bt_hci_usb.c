#include "bt_hci_usb.h"
#include "log.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#ifdef __PROSPERO__
#include <dev/usb/usb.h>
#include <dev/usb/usb_ioctl.h>
#include <dev/usb/usb_endian.h>
#endif

#define N_EVENT_RD 32
#define N_ACL_RD   16
#define N_XFER     (N_EVENT_RD + N_ACL_RD + 2)
#define RING_SIZE  64

typedef struct {
    uint8_t pkt[RING_SIZE][HCI_PKT_MAX];
    int len[RING_SIZE];
    int head, count;
} pkt_ring_t;

typedef struct {
    int fd;
    bt_hci_device_info_t info;
    pkt_ring_t events;
    pkt_ring_t acl;
    int is_open;
} hci_usb_state_t;

static hci_usb_state_t g_hci;

#ifdef __PROSPERO__
static void ring_push(pkt_ring_t *r, const uint8_t *data, int len)
{
    if (!r || !data || len <= 0 || len > HCI_PKT_MAX) return;
    if (r->count == RING_SIZE) {
        r->head = (r->head + 1) % RING_SIZE;
        r->count--;
    }
    int slot = (r->head + r->count) % RING_SIZE;
    memcpy(r->pkt[slot], data, (size_t)len);
    r->len[slot] = len;
    r->count++;
}
#endif

static int ring_pop(pkt_ring_t *r, uint8_t *out, int max_len)
{
    if (!r || !out || max_len <= 0) return 0;
    if (r->count == 0) return 0;
    int len = r->len[r->head] < max_len ? r->len[r->head] : max_len;
    memcpy(out, r->pkt[r->head], (size_t)len);
    r->head = (r->head + 1) % RING_SIZE;
    r->count--;
    return len;
}

/* Dynamic scanner for PS5 Bluetooth controller */
static int scan_bluetooth_chip(bt_hci_device_info_t *out)
{
    if (!out) return 0;
    char path[32];
#ifdef __PROSPERO__
    uint8_t buf[1024];
#endif

    /* Probe common ugen nodes across Fat, Slim, and Pro */
    for (int bus = 0; bus <= 3; bus++) {
        for (int dev = 1; dev <= 8; dev++) {
            snprintf(path, sizeof(path), "/dev/ugen%d.%d", bus, dev);
            int fd = open(path, O_RDWR | O_NONBLOCK);
            if (fd < 0) continue;

#ifdef __PROSPERO__
            struct usb_device_descriptor dd;
            if (ioctl(fd, USB_GET_DEVICE_DESC, &dd) == 0) {
                struct usb_gen_descriptor fdd;
                memset(&fdd, 0, sizeof(fdd));
                fdd.ugd_data = buf;
                fdd.ugd_maxlen = sizeof(buf);
                fdd.ugd_config_index = 0;

                if (ioctl(fd, USB_GET_FULL_DESC, &fdd) == 0 && fdd.ugd_actlen > 0) {
                    int len = (int)fdd.ugd_actlen;
                    if (len > (int)sizeof(buf)) len = (int)sizeof(buf);
                    int off = 0;
                    int in_bt_iface = 0;

                    while (off + 2 <= len) {
                        int dl = buf[off];
                        if (dl < 2 || off + dl > len) break;
                        uint8_t dtype = buf[off + 1];

                        /* Interface Descriptor */
                        if (dtype == 0x04 && dl >= 9) {
                            /* Class 0xE0 (Wireless), Subclass 0x01 (RF), Protocol 0x01 (Bluetooth) */
                            if (buf[off + 5] == 0xE0 && buf[off + 6] == 0x01 && buf[off + 7] == 0x01) {
                                in_bt_iface = 1;
                                out->iface = buf[off + 2];
                                snprintf(out->dev_node, sizeof(out->dev_node), "%s", path);
                            } else {
                                in_bt_iface = 0;
                            }
                        }
                        /* Endpoint Descriptor */
                        else if (dtype == 0x05 && dl >= 7 && in_bt_iface) {
                            uint8_t addr = buf[off + 2];
                            uint8_t attr = buf[off + 3] & 0x03;
                            uint16_t mps = (uint16_t)(buf[off + 4] | (buf[off + 5] << 8));

                            if (attr == 3 && (addr & 0x80) && !out->ep_events) {
                                out->ep_events = addr;
                                out->mps_events = mps;
                            } else if (attr == 2 && (addr & 0x80) && !out->ep_acl_in) {
                                out->ep_acl_in = addr;
                                out->mps_acl_in = mps;
                            } else if (attr == 2 && !(addr & 0x80) && !out->ep_acl_out) {
                                out->ep_acl_out = addr;
                                out->mps_acl_out = mps;
                            }
                        }
                        off += dl;
                    }

                    if (out->ep_events && out->ep_acl_in && out->ep_acl_out) {
                        close(fd);
                        log_line("bt_hci: Found PS5 Bluetooth chip at %s (iface %d, ep_ev=0x%02x, ep_in=0x%02x, ep_out=0x%02x)",
                                 out->dev_node, out->iface, out->ep_events, out->ep_acl_in, out->ep_acl_out);
                        return 1;
                    }
                }
            }
#endif
            close(fd);
        }
    }

    /* Fallback default for Fat PS5 firmware 10.01 / 13.60 */
    snprintf(out->dev_node, sizeof(out->dev_node), "/dev/ugen0.2");
    out->iface = 0;
    out->ep_events = 0x81;
    out->ep_acl_in = 0x82;
    out->ep_acl_out = 0x01;
    out->mps_events = 16;
    out->mps_acl_in = 1024;
    out->mps_acl_out = 1024;
    return 0;
}

int bt_hci_init(void)
{
    memset(&g_hci, 0, sizeof(g_hci));
    g_hci.fd = -1;
    scan_bluetooth_chip(&g_hci.info);

    g_hci.fd = open(g_hci.info.dev_node, O_RDWR | O_NONBLOCK);
    if (g_hci.fd < 0) {
        log_line("bt_hci: failed to open %s (errno=%d: %s)",
                 g_hci.info.dev_node, errno, strerror(errno));
        return 0;
    }

    g_hci.is_open = 1;
    log_line("bt_hci: Bluetooth controller opened successfully");
    return 1;
}

void bt_hci_poll(void)
{
    if (!g_hci.is_open || g_hci.fd < 0) return;

    /* In production on PS5, packets are received via USB_FS ring transfers */
#ifdef __PROSPERO__
    uint8_t buf[HCI_PKT_MAX];
    ssize_t n = read(g_hci.fd, buf, sizeof(buf));
    if (n > 0) {
        if (buf[0] == 0x04 && n >= 3) { /* HCI Event: code + length required */
            uint8_t event_len = buf[2];
            if ((ssize_t)event_len == n - 3) {
                ring_push(&g_hci.events, buf + 1, (int)(n - 1));
            }
        } else if (buf[0] == 0x02 && n >= 5) { /* HCI ACL header required */
            uint16_t acl_len = (uint16_t)((uint16_t)buf[3] |
                                         ((uint16_t)buf[4] << 8));
            if ((ssize_t)acl_len == n - 5) {
                ring_push(&g_hci.acl, buf + 1, (int)(n - 1));
            }
        }
    }
#endif
}

int bt_hci_send_cmd(uint16_t ocf, uint8_t ogf, const void *param, uint8_t param_len)
{
    if (!g_hci.is_open || g_hci.fd < 0 ||
        (param_len > 0 && !param) || param_len > 252u) return -1;

#ifdef __PROSPERO__
    struct usb_ctl_request req;
    uint8_t buf[256];

    memset(&req, 0, sizeof(req));
    req.ucr_request.bmRequestType = 0x20; /* Class / Device */
    req.ucr_request.bRequest = 0x00;
    req.ucr_request.wValue[0] = 0;
    req.ucr_request.wValue[1] = 0;
    req.ucr_request.wIndex[0] = (uint8_t)g_hci.info.iface;
    req.ucr_request.wIndex[1] = 0;

    buf[0] = (uint8_t)(ocf & 0xFF);
    buf[1] = (uint8_t)((ocf >> 8) | (ogf << 2));
    buf[2] = param_len;
    if (param && param_len > 0) {
        memcpy(buf + 3, param, param_len);
    }
    req.ucr_request.wLength[0] = (uint8_t)(3 + param_len);
    req.ucr_request.wLength[1] = 0;
    req.ucr_data = buf;

    if (ioctl(g_hci.fd, USB_DO_REQUEST, &req) < 0) {
        log_line("bt_hci: cmd 0x%02x/0x%04x failed (errno=%d)", ogf, ocf, errno);
        return -1;
    }
    return 0;
#else
    (void)ocf; (void)ogf; (void)param; (void)param_len;
    return 0;
#endif
}

int bt_hci_send_acl(uint16_t handle, uint8_t pb, const void *data, uint16_t len)
{
    if (!g_hci.is_open || g_hci.fd < 0 || !data ||
        handle == 0u || handle > 0x0effu || pb > 3u ||
        len > HCI_PKT_MAX - 4u) return -1;

#ifdef __PROSPERO__
    uint8_t pkt[HCI_PKT_MAX];
    uint16_t hdr = (handle & 0x0FFF) | ((pb & 0x03) << 12);
    pkt[0] = (uint8_t)(hdr & 0xFF);
    pkt[1] = (uint8_t)(hdr >> 8);
    pkt[2] = (uint8_t)(len & 0xFF);
    pkt[3] = (uint8_t)(len >> 8);
    memcpy(pkt + 4, data, len);

    ssize_t written = write(g_hci.fd, pkt, len + 4);
    return written == (ssize_t)(len + 4) ? 0 : -1;
#else
    (void)handle; (void)pb; (void)data; (void)len;
    return 0;
#endif
}

int bt_hci_recv_event(uint8_t *out, int max_len)
{
    return ring_pop(&g_hci.events, out, max_len);
}

int bt_hci_recv_acl(uint8_t *out, int max_len)
{
    return ring_pop(&g_hci.acl, out, max_len);
}

int bt_hci_is_open(void)
{
    return g_hci.is_open;
}

void bt_hci_close(void)
{
    if (g_hci.fd >= 0) {
        close(g_hci.fd);
        g_hci.fd = -1;
    }
    g_hci.is_open = 0;
}

const bt_hci_device_info_t *bt_hci_get_info(void)
{
    return &g_hci.info;
}
