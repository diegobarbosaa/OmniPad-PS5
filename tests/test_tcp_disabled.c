#include "../src/tcp_stream.h"

#include <assert.h>
#include <stdio.h>

void log_line(const char *format, ...)
{
    (void)format;
}

int main(void)
{
    assert(!tcp_stream_init(9045));
    tcp_stream_poll(0);
    tcp_stream_cleanup();
    puts("TCP debug input is disabled in the default build.");
    return 0;
}
