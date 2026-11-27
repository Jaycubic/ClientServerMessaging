#define _GNU_SOURCE
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "net.h"
#include "test_util.h"

static void pair(int sv[2])
{
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        perror("socketpair");
        exit(2);
    }
}

static void test_basic_lines(void)
{
    int sv[2]; pair(sv);
    line_reader_t lr; lr_init(&lr, sv[0]);
    char out[MAX_LINE + 1];

    net_write_all(sv[1], "one\ntwo\r\nthree\n", 15);
    CHECK(lr_read(&lr, out, sizeof out) == LR_LINE); CHECK(strcmp(out, "one") == 0);
    CHECK(lr_read(&lr, out, sizeof out) == LR_LINE); CHECK(strcmp(out, "two") == 0);
    CHECK(lr_read(&lr, out, sizeof out) == LR_LINE); CHECK(strcmp(out, "three") == 0);
    close(sv[0]); close(sv[1]);
}

static void test_partial_reads(void)
{
    int sv[2]; pair(sv);
    line_reader_t lr; lr_init(&lr, sv[0]);
    char out[MAX_LINE + 1];

    net_write_all(sv[1], "hel", 3);
    net_write_all(sv[1], "lo wor", 6);
    net_write_all(sv[1], "ld\n", 3);
    CHECK(lr_read(&lr, out, sizeof out) == LR_LINE);
    CHECK(strcmp(out, "hello world") == 0);
    close(sv[0]); close(sv[1]);
}

static void test_empty_line_and_eof(void)
{
    int sv[2]; pair(sv);
    line_reader_t lr; lr_init(&lr, sv[0]);
    char out[MAX_LINE + 1];

    net_write_all(sv[1], "\nlast\n", 6);
    close(sv[1]);
    CHECK(lr_read(&lr, out, sizeof out) == LR_LINE); CHECK(strcmp(out, "") == 0);
    CHECK(lr_read(&lr, out, sizeof out) == LR_LINE); CHECK(strcmp(out, "last") == 0);
    CHECK(lr_read(&lr, out, sizeof out) == LR_EOF);
    close(sv[0]);
}

static void test_too_long(void)
{
    int sv[2]; pair(sv);
    line_reader_t lr; lr_init(&lr, sv[0]);
    char out[MAX_LINE + 1];
    static char big[MAX_LINE * 3];
    memset(big, 'x', sizeof big);

    /* Writer in a child so a full socket buffer cannot deadlock the test. */
    if (fork() == 0) {
        net_write_all(sv[1], big, sizeof big);
        _exit(0);
    }
    CHECK(lr_read(&lr, out, sizeof out) == LR_TOO_LONG);
    close(sv[0]); close(sv[1]);
}

static void test_write_line_sanitizes(void)
{
    int sv[2]; pair(sv);
    line_reader_t lr; lr_init(&lr, sv[0]);
    char out[MAX_LINE + 1];

    CHECK(net_write_line(sv[1], "a\nb\rc") == 0);
    CHECK(lr_read(&lr, out, sizeof out) == LR_LINE);
    CHECK(strcmp(out, "a b c") == 0);
    close(sv[0]); close(sv[1]);
}

static void test_write_to_closed_peer_no_sigpipe(void)
{
    int sv[2]; pair(sv);
    close(sv[1]);
    CHECK(net_write_line(sv[0], "hi") == -1);   /* must not kill the process */
    close(sv[0]);
}

static void test_listen(void)
{
    int fd = net_listen(0);     /* ephemeral port */
    CHECK(fd >= 0);
    if (fd >= 0) close(fd);
}

int main(void)
{
    RUN(test_basic_lines);
    RUN(test_partial_reads);
    RUN(test_empty_line_and_eof);
    RUN(test_too_long);
    RUN(test_write_line_sanitizes);
    RUN(test_write_to_closed_peer_no_sigpipe);
    RUN(test_listen);
    TEST_DONE("test_net");
}
