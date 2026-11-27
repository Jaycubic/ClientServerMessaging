#define _GNU_SOURCE
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "log.h"
#include "net.h"
#include "session.h"
#include "test_util.h"

/* Open a session backed by a socketpair; peer[1] is the "client" end. */
static session_t *open_pair(int peer[2])
{
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, peer) != 0) {
        perror("socketpair");
        exit(2);
    }
    return session_open(peer[0], "test");
}

static void read_line_from(int fd, char *out, size_t n)
{
    line_reader_t lr;
    lr_init(&lr, fd);
    if (lr_read(&lr, out, n) != LR_LINE)
        out[0] = '\0';
}

static void test_login_and_duplicate(void)
{
    session_table_init();
    int p1[2], p2[2];
    session_t *a = open_pair(p1);
    session_t *b = open_pair(p2);
    CHECK(a && b);
    CHECK(session_count() == 2);
    CHECK(session_is_online("alice") == 0);       /* connected, not logged in */

    CHECK(session_login(a, "alice") == 0);
    CHECK(session_is_online("alice") == 1);
    CHECK(session_login(b, "alice") == -1);       /* duplicate rejected */
    CHECK(session_login(b, "bob") == 0);

    char list[256];
    CHECK(session_list(list, sizeof list) == 2);
    CHECK(strstr(list, "alice") && strstr(list, "bob") && strchr(list, ','));

    session_close(a);                             /* alice leaves ... */
    CHECK(session_is_online("alice") == 0);
    CHECK(session_count() == 1);
    CHECK(session_list(list, sizeof list) == 1 && strcmp(list, "bob") == 0);
    session_close(b);
    CHECK(session_count() == 0);
    close(p1[1]); close(p2[1]);
}

static void test_relogin_after_close(void)
{
    session_table_init();
    int p1[2], p2[2];
    session_t *a = open_pair(p1);
    CHECK(session_login(a, "alice") == 0);
    session_close(a);
    session_t *b = open_pair(p2);
    CHECK(session_login(b, "alice") == 0);        /* name is free again */
    session_close(b);
    close(p1[1]); close(p2[1]);
}

static void test_send_user_and_session(void)
{
    session_table_init();
    int p1[2], p2[2];
    session_t *a = open_pair(p1);
    session_t *b = open_pair(p2);
    session_login(a, "alice");
    session_login(b, "bob");

    char got[MAX_LINE + 1];
    CHECK(session_send_user("bob", "MSG|alice|hi") == 0);
    read_line_from(p2[1], got, sizeof got);
    CHECK(strcmp(got, "MSG|alice|hi") == 0);

    CHECK(session_send_user("nobody", "MSG|x|y") == -1);
    CHECK(session_send(a, "OK|hello") == 0);
    read_line_from(p1[1], got, sizeof got);
    CHECK(strcmp(got, "OK|hello") == 0);

    session_close(a); session_close(b);
    close(p1[1]); close(p2[1]);
}

static void test_broadcast_only_logged_in(void)
{
    session_table_init();
    int p1[2], p2[2], p3[2];
    session_t *a = open_pair(p1);
    session_t *b = open_pair(p2);
    session_t *c = open_pair(p3);                 /* connected, never logs in */
    session_login(a, "alice");
    session_login(b, "bob");

    CHECK(session_broadcast("SHUTDOWN|bye") == 2);
    char got[MAX_LINE + 1];
    read_line_from(p1[1], got, sizeof got); CHECK(strcmp(got, "SHUTDOWN|bye") == 0);
    read_line_from(p2[1], got, sizeof got); CHECK(strcmp(got, "SHUTDOWN|bye") == 0);

    session_shutdown_all();                       /* wakes everybody incl. c */
    char buf[8];
    CHECK(read(p3[1], buf, sizeof buf) == 0);     /* EOF seen by the client end */

    session_close(a); session_close(b); session_close(c);
    CHECK(session_wait_empty(1) == 1);
    close(p1[1]); close(p2[1]); close(p3[1]);
}

static void test_capacity(void)
{
    session_table_init();
    int (*pairs)[2] = malloc(sizeof(int[2]) * (MAX_CLIENTS + 1));
    session_t *ss[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; i++) {
        ss[i] = open_pair(pairs[i]);
        CHECK(ss[i] != NULL);
    }
    int extra[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, extra);
    CHECK(session_open(extra[0], "x") == NULL);   /* table full */
    CHECK(session_wait_empty(0) == 0);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        session_close(ss[i]);
        close(pairs[i][1]);
    }
    close(extra[0]); close(extra[1]);
    CHECK(session_count() == 0);
    free(pairs);
}

/* Many threads race to claim the same name: exactly one may win. */
#define RACERS 32
static session_t *racer_s[RACERS];
static pthread_barrier_t barrier;
static int racer_result[RACERS];

static void *racer(void *arg)
{
    long i = (long)arg;
    pthread_barrier_wait(&barrier);
    racer_result[i] = session_login(racer_s[i], "highlander");
    return NULL;
}

static void test_duplicate_login_race(void)
{
    for (int round = 0; round < 20; round++) {
        session_table_init();
        int peers[RACERS][2];
        pthread_t th[RACERS];
        pthread_barrier_init(&barrier, NULL, RACERS);
        for (int i = 0; i < RACERS; i++)
            racer_s[i] = open_pair(peers[i]);
        for (long i = 0; i < RACERS; i++)
            pthread_create(&th[i], NULL, racer, (void *)i);
        for (int i = 0; i < RACERS; i++)
            pthread_join(th[i], NULL);

        int winners = 0;
        for (int i = 0; i < RACERS; i++)
            if (racer_result[i] == 0)
                winners++;
        CHECK(winners == 1);
        char list[256];
        CHECK(session_list(list, sizeof list) == 1);

        for (int i = 0; i < RACERS; i++) {
            session_close(racer_s[i]);
            close(peers[i][1]);
        }
        pthread_barrier_destroy(&barrier);
    }
}

/* Concurrent senders to one user: every line arrives intact (no interleave). */
#define SENDERS 8
#define PER_SENDER 200
static void *flooder(void *arg)
{
    long id = (long)arg;
    for (int i = 0; i < PER_SENDER; i++) {
        char line[64];
        snprintf(line, sizeof line, "MSG|s%ld|payload-%d", id, i);
        session_send_user("sink", line);
    }
    return NULL;
}

static void test_concurrent_writes_not_interleaved(void)
{
    session_table_init();
    int p[2];
    session_t *sink = open_pair(p);
    session_login(sink, "sink");

    pthread_t th[SENDERS];
    for (long i = 0; i < SENDERS; i++)
        pthread_create(&th[i], NULL, flooder, (void *)i);

    line_reader_t lr;
    lr_init(&lr, p[1]);
    int counts[SENDERS] = {0};
    char got[MAX_LINE + 1];
    int bad = 0;
    for (int i = 0; i < SENDERS * PER_SENDER; i++) {
        if (lr_read(&lr, got, sizeof got) != LR_LINE) { bad++; break; }
        int id, seq;
        if (sscanf(got, "MSG|s%d|payload-%d", &id, &seq) != 2 || id < 0 || id >= SENDERS)
            bad++;
        else
            counts[id]++;
    }
    for (int i = 0; i < SENDERS; i++)
        pthread_join(th[i], NULL);
    CHECK(bad == 0);
    for (int i = 0; i < SENDERS; i++)
        CHECK(counts[i] == PER_SENDER);

    session_close(sink);
    close(p[1]);
}

int main(void)
{
    log_set_quiet(1);
    RUN(test_login_and_duplicate);
    RUN(test_relogin_after_close);
    RUN(test_send_user_and_session);
    RUN(test_broadcast_only_logged_in);
    RUN(test_capacity);
    RUN(test_duplicate_login_race);
    RUN(test_concurrent_writes_not_interleaved);
    TEST_DONE("test_session");
}
