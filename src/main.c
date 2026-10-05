#include "version.h"
#include "pad_types.h"
#include "ps5_vpad.h"
#include "usb_hotplug.h"
#include "web.h"
#include "tcp_stream.h"
#include "shellui_inject.h"
#include "log.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#ifdef __PROSPERO__
#include <ps5/kernel.h>
#endif

#define STATE_DIR   "/data/anypad"
#define LOG_PATH    STATE_DIR "/anypad.log"
#define STOP_FLAG   STATE_DIR "/stop"

static volatile sig_atomic_t g_running = 1;

static void handle_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static void check_flags(void)
{
    if (access(STOP_FLAG, F_OK) == 0) {
        unlink(STOP_FLAG);
        log_line("main: stop flag detected, shutting down");
        g_running = 0;
    }
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    mkdir("/data", 0755);
    mkdir("/user/data", 0755);
    mkdir(STATE_DIR, 0755);
    log_init(LOG_PATH);

    log_line("==================================================");
    log_line("%s v%s (Build %d)", ANYPAD_NAME, ANYPAD_VERSION, ANYPAD_BUILD);
    log_line("%s", ANYPAD_DESCRIPTION);
    log_line("Environment: FW 13.60 (kstuff-1.13-fpkg-dr-test5 / shadowmountplus v1.7 Beta 4)");
    log_line("==================================================");

    /* Signal any running previous instance to terminate */
    log_line("main: Terminating any previous instances...");
    for (int k = 0; k < 4; k++) {
        int sfd = open(STOP_FLAG, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (sfd >= 0) close(sfd);
        usleep(100000);
    }
    unlink(STOP_FLAG);
    usleep(100000);

    /* Kill previous PID if recorded */
    int pfd = open(STATE_DIR "/omnipad.pid", O_RDONLY);
    if (pfd >= 0) {
        char pbuf[32];
        ssize_t pr = read(pfd, pbuf, sizeof(pbuf) - 1);
        close(pfd);
        if (pr > 0) {
            pbuf[pr] = '\0';
            pid_t old_pid = (pid_t)atoi(pbuf);
            if (old_pid > 1 && old_pid != getpid()) {
                log_line("main: Terminating stale PID %d", (int)old_pid);
                kill(old_pid, SIGTERM);
                usleep(50000);
            }
        }
    }

    /* Record our PID */
    pfd = open(STATE_DIR "/omnipad.pid", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (pfd >= 0) {
        char pbuf[32];
        int plen = snprintf(pbuf, sizeof(pbuf), "%d\n", (int)getpid());
        write(pfd, pbuf, (size_t)plen);
        close(pfd);
    }

#ifdef __PROSPERO__
    uint32_t fw = kernel_get_fw_version();
    log_line("main: PS5 system detected, raw FW version 0x%08x (%x.%02x)",
             fw, (fw >> 24) & 0xFF, (fw >> 16) & 0xFF);
    if (fw > ANYPAD_MAX_FW) {
        log_line("main: WARNING - FW 0x%08x exceeds tested 13.60 maximum", fw);
    }
#else
    log_line("main: Running in host simulation/standalone mode");
#endif

    /* 1. Elevate credentials */
    if (!elevate_privileges()) {
        log_line("main: Warning: unable to elevate credentials");
    }

    /* 2. Initialize Virtual DualSense subsystem */
    if (!vpad_init()) {
        log_line("main: Fatal: vpad_init failed");
        return 1;
    }

    /* 3. Initialize USB Hotplug subsystem (2.4G Dongles & USB Cables) */
    if (!usb_hotplug_init()) {
        log_line("main: Warning: usb_hotplug_init failed (USB disabled)");
    }

    /* 4. Initialize Web Dashboard */
    if (!web_init(ANYPAD_DEFAULT_PORT)) {
        log_line("main: Warning: web_init failed");
    }

    /* 5. Initialize TCP frame testing stream */
    tcp_stream_init(ANYPAD_TCP_STREAM_PORT);

    notify_ps5("OmniPad PS5 v%s-b%d Active! Web UI: port %d", ANYPAD_VERSION, ANYPAD_BUILD, ANYPAD_DEFAULT_PORT);
    log_line("main: All subsystems initialized. Entering event loop.");

    while (g_running) {
        long now = now_ms();

        vpad_poll(now);
        usb_hotplug_poll(now);
        web_poll(now);
        tcp_stream_poll(now);

        check_flags();
        usleep(4000); /* ~250 Hz loop tick */
    }

    log_line("main: Shutting down OmniPad PS5...");
    tcp_stream_cleanup();
    web_cleanup();
    usb_hotplug_cleanup();
    vpad_cleanup_all();
    log_close();

    return 0;
}
