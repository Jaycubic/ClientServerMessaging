/* net.h - socket helpers: listening socket, safe writes, buffered line reader. */
#ifndef NET_H
#define NET_H

#include <stddef.h>
#include "protocol.h"

/* Create a TCP listening socket on all interfaces. Returns fd or -1. */
int net_listen(int port);

/* Write exactly n bytes (retries on EINTR/short writes, never raises SIGPIPE).
 * Returns 0 on success, -1 on error. */
int net_write_all(int fd, const char *buf, size_t n);

/* Write one protocol line (appends '\n'; embedded newlines become spaces;
 * truncated to MAX_LINE). Returns 0 on success, -1 on error. */
int net_write_line(int fd, const char *line);

/* Bound how long a send() to a stalled peer may block. */
int net_set_send_timeout(int fd, int seconds);

/* Buffered reader that turns a byte stream into lines. One per connection;
 * used by exactly one thread. */
typedef struct {
    int    fd;
    size_t len;
    char   buf[MAX_LINE + 2];
} line_reader_t;

#define LR_EOF       0
#define LR_LINE      1
#define LR_ERROR    (-1)
#define LR_TOO_LONG (-2)

void lr_init(line_reader_t *lr, int fd);

/* Read the next line into out (NUL-terminated, '\n'/'\r' stripped).
 * Returns LR_LINE, LR_EOF (peer closed), LR_ERROR, or LR_TOO_LONG. */
int lr_read(line_reader_t *lr, char *out, size_t outsz);

#endif
