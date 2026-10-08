#ifndef WEB_H
#define WEB_H

int web_init(int port);
void web_poll(long now);
void web_cleanup(void);

#ifdef WEB_TESTING
int web_test_bound_port(void);
int web_test_lan_auth_enabled(void);
#endif

#endif /* WEB_H */
