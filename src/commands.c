#define _GNU_SOURCE
#include "commands.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "db.h"
#include "log.h"
#include "users.h"

static void reply(session_t *s, const char *kind, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#include <stdarg.h>
static void reply(session_t *s, const char *kind, const char *fmt, ...)
{
    char body[MAX_LINE];
    char line[MAX_LINE + 16];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);
    snprintf(line, sizeof line, "%s|%s", kind, body);
    session_send(s, line);
}

/* Validate a target username argument. Returns 1 if usable, else replies. */
static int check_user_arg(session_t *s, const char *user)
{
    if (!users_name_wellformed(user) || !users_is_valid(user)) {
        reply(s, RSP_ERR, "Invalid user '%.*s'", MAX_USERNAME, user);
        return 0;
    }
    return 1;
}

static void do_send(session_t *s, char *args)
{
    char *sep = args ? strchr(args, '|') : NULL;
    if (!sep) {
        reply(s, RSP_ERR, "Usage: sendmessage <user> <message>");
        return;
    }
    *sep = '\0';
    const char *to = args;
    const char *text = sep + 1;

    if (!check_user_arg(s, to))
        return;
    if (strcmp(to, s->name) == 0) {
        reply(s, RSP_ERR, "You cannot send a message to yourself");
        return;
    }
    if (*text == '\0') {
        reply(s, RSP_ERR, "Message is empty");
        return;
    }
    if (strlen(text) > MAX_TEXT) {
        reply(s, RSP_ERR, "Message too long (max %d characters)", MAX_TEXT);
        return;
    }

    char push[MAX_LINE];
    snprintf(push, sizeof push, "%s|%s|%s", RSP_MSG, s->name, text);

    /* Online-check + store is atomic w.r.t. login + fetch-unread. */
    session_delivery_lock();
    int delivered = (session_send_user(to, push) == 0);
    int rc = db_save(s->name, to, text, delivered);
    session_delivery_unlock();

    if (rc != 0)
        reply(s, RSP_ERR, "Storage failure, message not saved");
    else if (delivered)
        reply(s, RSP_OK, "Delivered to %s", to);
    else
        reply(s, RSP_OK, "%s is offline, message stored", to);
    log_event("%s -> %s: message (%s)", s->name, to, delivered ? "delivered" : "queued");
}

static void do_getmessages(session_t *s, const char *user)
{
    if (!user) {
        reply(s, RSP_ERR, "Usage: getmessages <user>");
        return;
    }
    if (!check_user_arg(s, user))
        return;
    db_msg_t *msgs;
    int n;
    if (db_history(s->name, user, &msgs, &n) != 0) {
        reply(s, RSP_ERR, "Storage failure");
        return;
    }
    for (int i = 0; i < n; i++) {
        char line[MAX_LINE];
        snprintf(line, sizeof line, "%s|%s|%s|%ld|%s", RSP_HIST,
                 msgs[i].from, msgs[i].to, (long)msgs[i].ts, msgs[i].text);
        session_send(s, line);
    }
    db_free(msgs);
    reply(s, RSP_OK, "%d message(s) with %s", n, user);
}

static void do_deletemessages(session_t *s, const char *user)
{
    if (!user) {
        reply(s, RSP_ERR, "Usage: deletemessages <user>");
        return;
    }
    if (!check_user_arg(s, user))
        return;
    int n = db_delete(s->name, user);
    if (n < 0)
        reply(s, RSP_ERR, "Storage failure");
    else
        reply(s, RSP_OK, "Deleted %d message(s) with %s", n, user);
    log_event("%s deleted history with %s (%d)", s->name, user, n);
}

static void do_users(session_t *s)
{
    char list[MAX_LINE - 16];
    session_list(list, sizeof list);
    reply(s, RSP_USERLIST, "%s", list);
}

int cmd_handle(session_t *s, const char *line)
{
    char buf[MAX_LINE + 1];
    snprintf(buf, sizeof buf, "%s", line);

    char *args = strchr(buf, '|');
    if (args)
        *args++ = '\0';

    if (strcasecmp(buf, CMD_SEND) == 0)
        do_send(s, args);
    else if (strcasecmp(buf, CMD_GETMSGS) == 0)
        do_getmessages(s, args);
    else if (strcasecmp(buf, CMD_DELMSGS) == 0)
        do_deletemessages(s, args);
    else if (strcasecmp(buf, CMD_USERS) == 0)
        do_users(s);
    else if (strcasecmp(buf, CMD_QUIT) == 0) {
        reply(s, RSP_OK, "Goodbye");
        return CMD_CLOSE;
    } else if (strcasecmp(buf, CMD_CONNECT) == 0)
        reply(s, RSP_ERR, "Already connected as %s", s->name);
    else
        reply(s, RSP_ERR, "Unknown command");
    return CMD_CONTINUE;
}
