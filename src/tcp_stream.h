#ifndef TCP_STREAM_H
#define TCP_STREAM_H

int tcp_stream_init(int port);
void tcp_stream_poll(long now);
void tcp_stream_cleanup(void);

#if defined(OMNIPAD_ENABLE_TCP_DEBUG) && defined(TCP_STREAM_TESTING)
int tcp_stream_test_port(void);
#endif

#endif /* TCP_STREAM_H */
