/* client.c - command-line messaging client.
 *
 * Usage: client [-p port] [server_ip user]
 *
 * Two threads, as required by the brief:
 *   main thread     : reads what the user types (poll on stdin + wake pipe)
 *                     and sends protocol lines to the server
 *   receiver thread : blocks in recv(), prints everything the server sends
 *                     (real-time messages, replies, shutdown notice)
 * so an incoming message appears immediately even while the user is typing.
 *
 * The client stores nothing on disk; all state lives in the server.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "cli.h"
#include "net.h"

#define PROMPT          "> "
#define INBUF_SIZE      4096
#define HANDSHAKE_SECS  5
#define QUIT_WAIT_MS    3000

static int             sock_fd = -1;
static line_reader_t   reader;
static pthread_t       recv_thread;
static int             recv_started;
static int             wake_pipe[2];            /* receiver / signals -> main loop */
static int             sig_pipe[2];
static int             is_tty;
static pthread_mutex_t ui_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_int      quitting;                /* we initiated the disconnect */
static atomic_int      got_shutdown_notice;     /* server told us it is stopping */
static atomic_int      lost_connection;         /* connection died unexpectedly */
static int             default_port = DEFAULT_PORT;

/* ---------------- terminal output ---------------- */

static void ui_prompt_locked(void)
{
    if (is_tty) {
        fputs(PROMPT, stdout);
        fflush(stdout);
    }
}

/* Print text that may arrive while the user is typing, then redraw prompt. */
static void ui_print(const char *text)
{
    pthread_mutex_lock(&ui_lock);
    if (is_tty)
        fputs("\r\033[K", stdout);              /* clear the half-typed prompt line */
    fputs(text, stdout);
    fputc('\n', stdout);
    ui_prompt_locked();
    fflush(stdout);
    pthread_mutex_unlock(&ui_lock);
}

static void ui_prompt(void)
{
    pthread_mutex_lock(&ui_lock);
    ui_prompt_locked();
    pthread_mutex_unlock(&ui_lock);
}

/* ---------------- receiver thread ---------------- */

static void wake_main(void)
{
    unsigned char b = 1;
    ssize_t r = write(wake_pipe[1], &b, 1);
    (void)r;
}

static void *receiver(void *arg)
{
    (void)arg;
    char line[MAX_LINE + 1], text[MAX_LINE + 64];
    int r;
    while ((r = lr_read(&reader, line, sizeof line)) == LR_LINE) {
        if (cli_format(line, text, sizeof text) == FMT_SHUTDOWN)
            atomic_store(&got_shutdown_notice, 1);
        ui_print(text);
    }
    if (!atomic_load(&quitting) && !atomic_load(&got_shutdown_notice))
        atomic_store(&lost_connection, 1);
    wake_main();
    return NULL;
}

/* ---------------- connection ---------------- */

static void drop_connection(void)
{
    if (sock_fd >= 0)
        shutdown(sock_fd, SHUT_RDWR);           /* wakes the receiver's recv() */
    if (recv_started) {
        pthread_join(recv_thread, NULL);
        recv_started = 0;
    }
    if (sock_fd >= 0) {
        close(sock_fd);
        sock_fd = -1;
    }
}

static int tcp_connect(const char *host, int port)
{
    char portstr[16];
    snprintf(portstr, sizeof portstr, "%d", port);
    struct addrinfo hints, *res, *ai;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0)
        return -1;
    int fd = -1;
    for (ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;
        struct timeval tv = { .tv_sec = HANDSHAKE_SECS };
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);   /* bounds connect() */
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

/* Log in. On success the welcome and unread messages are printed and the
 * receiver thread is running. Returns 0 on success, -1 on failure. */
static int do_connect(const cli_result_t *c)
{
    int port = c->port ? c->port : default_port;
    int fd = tcp_connect(c->host, port);
    if (fd < 0) {
        char msg[400];
        snprintf(msg, sizeof msg, "Error: cannot connect to %s:%d", c->host, port);
        ui_print(msg);
        return -1;
    }

    struct timeval tv = { .tv_sec = HANDSHAKE_SECS };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    lr_init(&reader, fd);

    char line[MAX_LINE + 1], text[MAX_LINE + 64], req[MAX_USERNAME + 16];
    snprintf(req, sizeof req, "%s|%s", CMD_CONNECT, c->user);
    if (net_write_line(fd, req) != 0 || lr_read(&reader, line, sizeof line) != LR_LINE) {
        ui_print("Error: no response from server");
        close(fd);
        return -1;
    }
    cli_format(line, text, sizeof text);
    if (strncmp(line, RSP_OK "|", 3) != 0) {         /* ERR|... : refused */
        ui_print(text);
        close(fd);
        return -1;
    }
    ui_print(text);

    /* "OK|Welcome <user> (<n> unread)" is followed by exactly n UNREAD lines. */
    int unread = 0;
    const char *p = strrchr(line, '(');
    if (p)
        sscanf(p, "(%d unread)", &unread);
    for (int i = 0; i < unread; i++) {
        if (lr_read(&reader, line, sizeof line) != LR_LINE)
            break;
        cli_format(line, text, sizeof text);
        ui_print(text);
    }

    tv.tv_sec = 0;                                   /* receiver blocks indefinitely */
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    atomic_store(&quitting, 0);
    atomic_store(&got_shutdown_notice, 0);
    atomic_store(&lost_connection, 0);
    sock_fd = fd;
    if (pthread_create(&recv_thread, NULL, receiver, NULL) != 0) {
        ui_print("Error: cannot start receiver thread");
        close(fd);
        sock_fd = -1;
        return -1;
    }
    recv_started = 1;
    return 0;
}

/* ---------------- main loop ---------------- */

static void on_signal(int sig)
{
    unsigned char b = (unsigned char)sig;
    int saved = errno;
    ssize_t r = write(sig_pipe[1], &b, 1);
    (void)r;
    errno = saved;
}

static void drain(int fd)
{
    unsigned char buf[64];
    ssize_t r = read(fd, buf, sizeof buf);
    (void)r;
}

/* Ask the server to close, wait briefly for it, then tear down. */
static void graceful_disconnect(void)
{
    if (sock_fd < 0)
        return;
    atomic_store(&quitting, 1);
    net_write_line(sock_fd, CMD_QUIT);
    struct pollfd p = { .fd = wake_pipe[0], .events = POLLIN };
    poll(&p, 1, QUIT_WAIT_MS);                       /* receiver wakes us on EOF */
    drop_connection();
}

/* Handle one complete line of user input. Returns 1 if the client should exit. */
static int handle_input(const char *input)
{
    cli_result_t res;
    cli_parse(input, sock_fd >= 0, &res);

    switch (res.kind) {
    case CLI_EMPTY:
        break;
    case CLI_HELP:
        ui_print(cli_help_text());
        break;
    case CLI_ERROR:
        {
            char msg[sizeof res.error + 8];
            snprintf(msg, sizeof msg, "Error: %s", res.error);
            ui_print(msg);
        }
        break;
    case CLI_CONNECT:
        do_connect(&res);
        break;
    case CLI_SEND:
        if (net_write_line(sock_fd, res.line) != 0)
            ui_print("Error: connection to server lost");
        break;
    case CLI_SHUTDOWN:
        graceful_disconnect();
        return 1;
    }
    return 0;
}

static void usage(const char *prog)
{
    fprintf(stderr, "usage: %s [-p port] [server_ip user]\n", prog);
}

int main(int argc, char **argv)
{
    int opt;
    while ((opt = getopt(argc, argv, "p:h")) != -1) {
        if (opt == 'p') {
            default_port = atoi(optarg);
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (default_port < 1 || default_port > 65535 ||
        (argc - optind != 0 && argc - optind != 2)) {
        usage(argv[0]);
        return 2;
    }

    is_tty = isatty(STDIN_FILENO);
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGPIPE, SIG_IGN);
    if (pipe(wake_pipe) != 0 || pipe(sig_pipe) != 0) {
        perror("pipe");
        return 1;
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    ui_print("Messaging client. Type 'help' for commands.");
    if (argc - optind == 2) {                        /* convenience: client <ip> <user> */
        char cmd[512];
        snprintf(cmd, sizeof cmd, "connect %s %s", argv[optind], argv[optind + 1]);
        handle_input(cmd);
    }

    char inbuf[INBUF_SIZE];
    size_t inlen = 0;
    int exit_code = 0, running = 1, stdin_open = 1;

    ui_prompt();
    while (running) {
        struct pollfd fds[3];
        int n = 0;
        fds[n++] = (struct pollfd){ .fd = wake_pipe[0], .events = POLLIN };
        fds[n++] = (struct pollfd){ .fd = sig_pipe[0],  .events = POLLIN };
        if (stdin_open)
            fds[n++] = (struct pollfd){ .fd = STDIN_FILENO, .events = POLLIN };

        if (poll(fds, (nfds_t)n, -1) < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        if (fds[1].revents & POLLIN) {               /* Ctrl+C / SIGTERM */
            drain(sig_pipe[0]);
            graceful_disconnect();
            break;
        }
        if (fds[0].revents & POLLIN) {               /* receiver thread ended */
            drain(wake_pipe[0]);
            if (sock_fd >= 0) {
                int notice = atomic_load(&got_shutdown_notice);
                drop_connection();
                if (notice) {
                    ui_print("Disconnected: server has shut down.");
                } else {
                    ui_print("Disconnected: connection to server lost.");
                    exit_code = 1;
                }
                break;                               /* nothing left to talk to */
            }
        }
        if (stdin_open && (fds[n - 1].revents & (POLLIN | POLLHUP))) {
            ssize_t r = read(STDIN_FILENO, inbuf + inlen, sizeof inbuf - inlen - 1);
            if (r <= 0) {                            /* EOF (Ctrl+D or end of pipe) */
                stdin_open = 0;
                graceful_disconnect();
                break;
            }
            inlen += (size_t)r;
            char *nl;
            while (running && (nl = memchr(inbuf, '\n', inlen)) != NULL) {
                *nl = '\0';
                if (handle_input(inbuf))
                    running = 0;
                size_t used = (size_t)(nl - inbuf) + 1;
                memmove(inbuf, inbuf + used, inlen - used);
                inlen -= used;
            }
            if (running && inlen >= sizeof inbuf - 1) {
                ui_print("Error: input line too long, discarded");
                inlen = 0;
            }
            if (running)
                ui_prompt();
        }
    }

    drop_connection();
    if (is_tty)
        fputc('\n', stdout);
    return exit_code;
}
