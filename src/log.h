#ifndef LOG_H
#define LOG_H

#include <stddef.h>

void log_init(const char *path);
void log_line(const char *fmt, ...);
void log_close(void);
void notify_ps5(const char *fmt, ...);

#endif /* LOG_H */
