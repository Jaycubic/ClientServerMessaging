#define _GNU_SOURCE
#include "client_handler.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "commands.h"
#include "db.h"
#include "log.h"
#include "net.h"
#include "session.h"
#include "users.h"

/* Expect "CONNECT|<user>". On success the session is logged in, the welcome
 * and any unread messages have been sent. Returns 0 on success, -1 on failure
 * (an ERR line has been sent; the caller closes the connection). */
static int do_login(session_t *s, const char *line)
{
    char buf[MAX_LINE + 1];
    snprintf(buf, sizeof buf, "%s", line);

    char *name = strchr(buf, '|');
    if (!name) {
        session_send(s, "ERR|Expected CONNECT|<user>");
        return -1;
    }
    *name++ = '\0';
    if (strcasecmp(buf, CMD_CONNECT) != 0) {
        session_send(s, "ERR|Not connected: send CONNECT|<user> first");
        return -1;
    }
    if (!users_name_wellformed(name) || !users_is_valid(name)) {
        session_send(s, "ERR|Invalid username");
        log_event("rejected login from %s: invalid user", s->addr);
        return -1;
    }

    /* Login + unread fetch must not interleave with a concurrent SEND. */
    session_delivery_lock();
    if (session_login(s, name) != 0) {
        session_delivery_unlock();
        session_send(s, "ERR|User already connected");
        log_event("rejected login from %s: '%s' already connected", s->addr, name);
        return -1;
    }

    db_msg_t *unread = NULL;
    int n = 0;
    if (db_take_unread(name, &unread, &n) != 0)
        n = 0;

    char out[MAX_LINE];
    snprintf(out, sizeof out, "OK|Welcome %s (%d unread)", name, n);
    session_send(s, out);
    for (int i = 0; i < n; i++) {
        snprintf(out, sizeof out, "%s|%s|%ld|%s", RSP_UNREAD,
                 unread[i].from, (long)unread[i].ts, unread[i].text);
        session_send(s, out);
    }
    session_delivery_unlock();
    db_free(unread);

    log_event("user %s connected from %s", name, s->addr);
    return 0;
}

void *client_thread(void *arg)
{
    session_t *s = arg;
    line_reader_t lr;
    char line[MAX_LINE + 1];
    int logged_in = 0;

    lr_init(&lr, s->fd);
    for (;;) {
        int r = lr_read(&lr, line, sizeof line);
        if (r == LR_TOO_LONG) {
            session_send(s, "ERR|Line too long");
            break;
        }
        if (r != LR_LINE)          /* EOF or error: peer gone / server shutdown */
            break;

        if (!logged_in) {
            if (do_login(s, line) != 0)
                break;
            logged_in = 1;
            continue;
        }
        if (cmd_handle(s, line) == CMD_CLOSE)
            break;
    }

    if (logged_in)
        log_event("user %s disconnected", s->name);
    session_close(s);
    return NULL;
}
