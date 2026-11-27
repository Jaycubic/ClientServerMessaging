#define _GNU_SOURCE
#include "net.h"

#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

int net_listen(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0 ||
        listen(fd, 64) < 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    return fd;
}

int net_write_all(int fd, const char *buf, size_t n)
{
    size_t off = 0;
    while (off < n) {
        ssize_t w = send(fd, buf + off, n - off, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

int net_write_line(int fd, const char *line)
{
    char out[MAX_LINE + 1];
    size_t n = 0;
    for (; line[n] != '\0' && n < MAX_LINE; n++)
        out[n] = (line[n] == '\n' || line[n] == '\r') ? ' ' : line[n];
    out[n++] = '\n';
    return net_write_all(fd, out, n);
}

int net_set_send_timeout(int fd, int seconds)
{
    struct timeval tv = { .tv_sec = seconds, .tv_usec = 0 };
    return setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}

void lr_init(line_reader_t *lr, int fd)
{
    lr->fd = fd;
    lr->len = 0;
}

int lr_read(line_reader_t *lr, char *out, size_t outsz)
{
    for (;;) {
        char *nl = memchr(lr->buf, '\n', lr->len);
        if (nl) {
            size_t linelen = (size_t)(nl - lr->buf);
            size_t consumed = linelen + 1;
            if (linelen > 0 && lr->buf[linelen - 1] == '\r')
                linelen--;
            if (linelen >= outsz || linelen > MAX_LINE)
                return LR_TOO_LONG;
            memcpy(out, lr->buf, linelen);
            out[linelen] = '\0';
            memmove(lr->buf, lr->buf + consumed, lr->len - consumed);
            lr->len -= consumed;
            return LR_LINE;
        }
        if (lr->len > MAX_LINE)       /* no newline within the limit */
            return LR_TOO_LONG;

        ssize_t r = recv(lr->fd, lr->buf + lr->len, sizeof lr->buf - lr->len, 0);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return LR_ERROR;
        }
        if (r == 0)
            return LR_EOF;
        lr->len += (size_t)r;
    }
}
