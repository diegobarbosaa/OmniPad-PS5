#ifndef WEB_H
#define WEB_H

int web_init(int port);
void web_poll(long now);
void web_cleanup(void);

#endif /* WEB_H */
