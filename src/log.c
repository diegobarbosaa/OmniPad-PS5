#include "log.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef __PROSPERO__
#include <ps5/kernel.h>
#include <ps5/klog.h>
#endif

typedef struct {
    char reserved[45];
    char message[3075];
} notify_request_t;

#ifdef __PROSPERO__
extern int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);
#endif

static FILE *g_log_file = NULL;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

void log_init(const char *path)
{
    pthread_mutex_lock(&g_log_mutex);
    if (g_log_file) fclose(g_log_file);
    mkdir("/data/anypad", 0755);
    g_log_file = fopen(path, "a");
    pthread_mutex_unlock(&g_log_mutex);
}

void log_line(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    struct timeval tv;
    struct tm tm;

    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &tm);

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    pthread_mutex_lock(&g_log_mutex);
    printf("[%02d:%02d:%02d.%03d] %s\n",
           tm.tm_hour, tm.tm_min, tm.tm_sec, (int)(tv.tv_usec / 1000), buf);
    fflush(stdout);

    if (g_log_file) {
        fprintf(g_log_file, "[%02d:%02d:%02d.%03d] %s\n",
                tm.tm_hour, tm.tm_min, tm.tm_sec, (int)(tv.tv_usec / 1000), buf);
        fflush(g_log_file);
    }
    pthread_mutex_unlock(&g_log_mutex);

#ifdef __PROSPERO__
    klog_printf("[OmniPad] %s\n", buf);
#endif
}

void log_close(void)
{
    pthread_mutex_lock(&g_log_mutex);
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
    pthread_mutex_unlock(&g_log_mutex);
}

void notify_ps5(const char *fmt, ...)
{
#ifdef __PROSPERO__
    notify_request_t req;
    va_list ap;

    memset(&req, 0, sizeof(req));
    va_start(ap, fmt);
    vsnprintf(req.message, sizeof(req.message), fmt, ap);
    va_end(ap);

    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
#else
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("[NOTIFY] %s\n", buf);
#endif
}
