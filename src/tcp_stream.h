#ifndef TCP_STREAM_H
#define TCP_STREAM_H

int tcp_stream_init(int port);
void tcp_stream_poll(long now);
void tcp_stream_cleanup(void);

#endif /* TCP_STREAM_H */
