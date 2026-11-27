/* server.c - entry point: accept loop, thread-per-client, graceful shutdown.
 *
 * Usage: server [-p port] [-u users_file] [-d data_file]
 *
 * Shutdown: SIGINT/SIGTERM (Ctrl+C) or typing "shutdown" on the server console.
 * The signal handler only writes a byte to a self-pipe (async-signal-safe);
 * the main loop poll()s the listening socket and that pipe together.
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "client_handler.h"
#include "db.h"
#include "log.h"
#include "net.h"
#include "protocol.h"
#include "session.h"
#include "users.h"

#define SEND_TIMEOUT_SECS 3
#define SHUTDOWN_WAIT_SECS 5

static int wake_pipe[2] = { -1, -1 };

static void on_signal(int sig)
{
    int saved = errno;
    unsigned char b = (unsigned char)sig;
    ssize_t r = write(wake_pipe[1], &b, 1);
    (void)r;
    errno = saved;
}

static void usage(const char *prog)
{
    fprintf(stderr, "usage: %s [-p port] [-u users_file] [-d data_file]\n", prog);
}

/* Read one console line. Returns 1 if the operator asked to shut down,
 * -1 if stdin is closed, else 0. */
static int handle_console(void)
{
    char cmd[128];
    if (!fgets(cmd, sizeof cmd, stdin))
        return -1;
    cmd[strcspn(cmd, "\r\n")] = '\0';
    if (strcmp(cmd, "shutdown") == 0 || strcmp(cmd, "quit") == 0) {
        log_event("Shutdown requested from console");
        return 1;
    }
    if (strcmp(cmd, "users") == 0) {
        char list[MAX_LINE];
        int n = session_list(list, sizeof list);
        log_event("%d online: %s", n, list);
    } else if (cmd[0] != '\0') {
        log_event("console commands: users, shutdown");
    }
    return 0;
}

int main(int argc, char **argv)
{
    int port = DEFAULT_PORT;
    const char *users_file = "users.txt";
    const char *data_file = "messages.dat";

    int opt;
    while ((opt = getopt(argc, argv, "p:u:d:h")) != -1) {
        switch (opt) {
        case 'p': port = atoi(optarg); break;
        case 'u': users_file = optarg; break;
        case 'd': data_file = optarg; break;
        default:  usage(argv[0]); return 2;
        }
    }
    if (port < 0 || port > 65535) {
        usage(argv[0]);
        return 2;
    }

    int nusers = users_load(users_file);
    if (nusers < 0) {
        fprintf(stderr, "cannot read user list '%s': %s\n", users_file, strerror(errno));
        return 1;
    }
    if (db_init(data_file) != 0) {
        fprintf(stderr, "cannot initialise message storage '%s': %s\n", data_file, strerror(errno));
        return 1;
    }
    session_table_init();

    if (pipe(wake_pipe) != 0) {
        perror("pipe");
        return 1;
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    int lfd = net_listen(port);
    if (lfd < 0) {
        perror("listen");
        return 1;
    }
    if (port == 0) {                       /* report the ephemeral port chosen */
        struct sockaddr_in a;
        socklen_t alen = sizeof a;
        getsockname(lfd, (struct sockaddr *)&a, &alen);
        port = ntohs(a.sin_port);
    }
    log_event("Listening on port %d (%d valid users). Ctrl+C or 'shutdown' to stop.",
              port, nusers);

    int console = isatty(STDIN_FILENO);
    int running = 1;
    while (running) {
        struct pollfd fds[3];
        int nfds = 0;
        fds[nfds++] = (struct pollfd){ .fd = lfd,          .events = POLLIN };
        fds[nfds++] = (struct pollfd){ .fd = wake_pipe[0], .events = POLLIN };
        if (console)
            fds[nfds++] = (struct pollfd){ .fd = STDIN_FILENO, .events = POLLIN };

        if (poll(fds, (nfds_t)nfds, -1) < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            break;
        }
        if (fds[1].revents & POLLIN) {
            log_event("Signal received");
            running = 0;
            break;
        }
        if (console && (fds[2].revents & (POLLIN | POLLHUP))) {
            int c = handle_console();
            if (c == 1) {
                running = 0;
                break;
            }
            if (c < 0)
                console = 0;               /* stdin closed: ignore the console */
        }
        if (fds[0].revents & POLLIN) {
            struct sockaddr_in ca;
            socklen_t clen = sizeof ca;
            int cfd = accept(lfd, (struct sockaddr *)&ca, &clen);
            if (cfd < 0) {
                if (errno != EINTR && errno != ECONNABORTED)
                    usleep(10000);         /* e.g. EMFILE: avoid a busy loop */
                continue;
            }
            char addr[64];
            snprintf(addr, sizeof addr, "%s:%d", inet_ntoa(ca.sin_addr), ntohs(ca.sin_port));
            net_set_send_timeout(cfd, SEND_TIMEOUT_SECS);

            session_t *s = session_open(cfd, addr);
            if (!s) {
                net_write_line(cfd, "ERR|Server full");
                close(cfd);
                log_event("rejected %s: server full", addr);
                continue;
            }
            pthread_t tid;
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            if (pthread_create(&tid, &attr, client_thread, s) != 0) {
                net_write_line(cfd, "ERR|Server error");
                session_close(s);
                log_event("thread creation failed for %s", addr);
            }
            pthread_attr_destroy(&attr);
        }
    }

    /* ---- graceful shutdown ---- */
    log_event("Server shutting down...");
    close(lfd);                                    /* stop accepting */
    int n = session_broadcast(RSP_SHUTDOWN "|" SHUTDOWN_TEXT);
    log_event("Shutdown notice sent to %d client(s)", n);
    session_shutdown_all();                        /* wake blocked readers */
    if (!session_wait_empty(SHUTDOWN_WAIT_SECS))
        log_event("Warning: %d connection(s) did not close in time", session_count());
    db_close();
    log_event("Shutdown complete");
    return 0;
}
