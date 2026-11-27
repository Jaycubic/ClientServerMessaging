#define _GNU_SOURCE
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "commands.h"
#include "db.h"
#include "log.h"
#include "net.h"
#include "session.h"
#include "test_util.h"
#include "users.h"

typedef struct {
    session_t *s;
    int        peer;       /* client-side end of the socketpair */
    line_reader_t lr;
} user_t;

static void login(user_t *u, const char *name)
{
    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    u->s = session_open(sv[0], "test");
    u->peer = sv[1];
    lr_init(&u->lr, u->peer);
    session_login(u->s, name);
}

static void logout(user_t *u)
{
    session_close(u->s);
    close(u->peer);
}

/* Read the next line the server wrote to this user. */
static const char *next(user_t *u)
{
    static char buf[MAX_LINE + 1];
    if (lr_read(&u->lr, buf, sizeof buf) != LR_LINE)
        buf[0] = '\0';
    return buf;
}

static void fresh(void)
{
    session_table_init();
    db_init(NULL);
    users_load("users.txt");
}

static void test_send_to_online_user(void)
{
    fresh();
    user_t a, b; login(&a, "alice"); login(&b, "bob");
    CHECK(cmd_handle(a.s, "SEND|bob|hello there") == CMD_CONTINUE);
    CHECK(strcmp(next(&b), "MSG|alice|hello there") == 0);
    CHECK(strncmp(next(&a), "OK|Delivered", 12) == 0);
    logout(&a); logout(&b);
}

static void test_text_may_contain_pipes(void)
{
    fresh();
    user_t a, b; login(&a, "alice"); login(&b, "bob");
    cmd_handle(a.s, "SEND|bob|a|b|c");
    CHECK(strcmp(next(&b), "MSG|alice|a|b|c") == 0);
    logout(&a); logout(&b);
}

static void test_send_errors(void)
{
    fresh();
    user_t a; login(&a, "alice");
    cmd_handle(a.s, "SEND|nobody|hi");      CHECK(strncmp(next(&a), "ERR|Invalid user", 16) == 0);
    cmd_handle(a.s, "SEND|alice|hi");       CHECK(strncmp(next(&a), "ERR|You cannot", 14) == 0);
    cmd_handle(a.s, "SEND|bob|");           CHECK(strncmp(next(&a), "ERR|Message is empty", 20) == 0);
    cmd_handle(a.s, "SEND|bob");            CHECK(strncmp(next(&a), "ERR|Usage", 9) == 0);
    cmd_handle(a.s, "SEND");                CHECK(strncmp(next(&a), "ERR|Usage", 9) == 0);
    cmd_handle(a.s, "SEND|b|ob|x");         CHECK(strncmp(next(&a), "ERR|Invalid user", 16) == 0);
    logout(&a);
}

static void test_offline_message_is_stored(void)
{
    fresh();
    user_t a; login(&a, "alice");
    cmd_handle(a.s, "SEND|bob|are you there");
    CHECK(strncmp(next(&a), "OK|bob is offline", 17) == 0);

    db_msg_t *m; int n;
    CHECK(db_take_unread("bob", &m, &n) == 0);
    CHECK(n == 1 && strcmp(m[0].from, "alice") == 0 && strcmp(m[0].text, "are you there") == 0);
    db_free(m);
    CHECK(db_take_unread("bob", &m, &n) == 0 && n == 0);   /* only delivered once */
    db_free(m);
    logout(&a);
}

static void test_getmessages_and_delete(void)
{
    fresh();
    user_t a, b; login(&a, "alice"); login(&b, "bob");
    cmd_handle(a.s, "SEND|bob|one");  next(&b); next(&a);
    cmd_handle(b.s, "SEND|alice|two"); next(&a); next(&b);

    cmd_handle(a.s, "GETMSGS|bob");
    const char *l1 = next(&a); CHECK(strncmp(l1, "HIST|alice|bob|", 15) == 0 && strstr(l1, "|one"));
    const char *l2 = next(&a); CHECK(strncmp(l2, "HIST|bob|alice|", 15) == 0 && strstr(l2, "|two"));
    CHECK(strncmp(next(&a), "OK|2 message(s)", 15) == 0);

    cmd_handle(a.s, "GETMSGS|nobody");  CHECK(strncmp(next(&a), "ERR|Invalid user", 16) == 0);
    cmd_handle(a.s, "DELMSGS|nobody");  CHECK(strncmp(next(&a), "ERR|Invalid user", 16) == 0);

    cmd_handle(a.s, "DELMSGS|bob");     CHECK(strncmp(next(&a), "OK|Deleted 2", 12) == 0);
    cmd_handle(a.s, "GETMSGS|bob");     CHECK(strncmp(next(&a), "OK|0 message(s)", 15) == 0);
    logout(&a); logout(&b);
}

static void test_users_quit_unknown(void)
{
    fresh();
    user_t a, b; login(&a, "alice"); login(&b, "bob");
    cmd_handle(a.s, "USERS");
    const char *l = next(&a);
    CHECK(strncmp(l, "USERLIST|", 9) == 0 && strstr(l, "alice") && strstr(l, "bob"));

    cmd_handle(a.s, "BOGUS|x");           CHECK(strcmp(next(&a), "ERR|Unknown command") == 0);
    cmd_handle(a.s, "");                  CHECK(strcmp(next(&a), "ERR|Unknown command") == 0);
    cmd_handle(a.s, "GETMSGS");           CHECK(strncmp(next(&a), "ERR|Usage", 9) == 0);
    cmd_handle(a.s, "DELMSGS");           CHECK(strncmp(next(&a), "ERR|Usage", 9) == 0);
    cmd_handle(a.s, "CONNECT|bob");       CHECK(strncmp(next(&a), "ERR|Already connected", 20) == 0);
    CHECK(cmd_handle(a.s, "QUIT") == CMD_CLOSE);
    CHECK(strcmp(next(&a), "OK|Goodbye") == 0);
    logout(&a); logout(&b);
}

int main(void)
{
    log_set_quiet(1);
    RUN(test_send_to_online_user);
    RUN(test_text_may_contain_pipes);
    RUN(test_send_errors);
    RUN(test_offline_message_is_stored);
    RUN(test_getmessages_and_delete);
    RUN(test_users_quit_unknown);
    TEST_DONE("test_commands");
}
