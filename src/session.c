#define _GNU_SOURCE
#include "session.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "net.h"

static session_t       table[MAX_CLIENTS];
static pthread_mutex_t table_lock    = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  table_empty   = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t delivery_lock = PTHREAD_MUTEX_INITIALIZER;
static int             used_slots;

void session_table_init(void)
{
    pthread_mutex_lock(&table_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        memset(&table[i], 0, sizeof table[i]);
        table[i].fd = -1;
        pthread_mutex_init(&table[i].wlock, NULL);
    }
    used_slots = 0;
    pthread_mutex_unlock(&table_lock);
}

session_t *session_open(int fd, const char *addr)
{
    session_t *found = NULL;
    pthread_mutex_lock(&table_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (table[i].state == SLOT_FREE) {
            table[i].state = SLOT_CONNECTED;
            table[i].fd = fd;
            table[i].name[0] = '\0';
            snprintf(table[i].addr, sizeof table[i].addr, "%s", addr ? addr : "?");
            used_slots++;
            found = &table[i];
            break;
        }
    }
    pthread_mutex_unlock(&table_lock);
    return found;
}

int session_login(session_t *s, const char *name)
{
    int rc = 0;
    pthread_mutex_lock(&table_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (table[i].state == SLOT_LOGGED_IN && strcmp(table[i].name, name) == 0) {
            rc = -1;                       /* duplicate session */
            break;
        }
    }
    if (rc == 0) {
        snprintf(s->name, sizeof s->name, "%s", name);
        s->state = SLOT_LOGGED_IN;
    }
    pthread_mutex_unlock(&table_lock);
    return rc;
}

void session_close(session_t *s)
{
    pthread_mutex_lock(&table_lock);
    if (s->state != SLOT_FREE) {
        s->state = SLOT_FREE;
        s->name[0] = '\0';
        if (s->fd >= 0)
            close(s->fd);
        s->fd = -1;
        used_slots--;
        if (used_slots == 0)
            pthread_cond_broadcast(&table_empty);
    }
    pthread_mutex_unlock(&table_lock);
}

int session_send(session_t *s, const char *line)
{
    pthread_mutex_lock(&s->wlock);
    int rc = (s->fd >= 0) ? net_write_line(s->fd, line) : -1;
    pthread_mutex_unlock(&s->wlock);
    return rc;
}

int session_send_user(const char *name, const char *line)
{
    int rc = -1;
    pthread_mutex_lock(&table_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (table[i].state == SLOT_LOGGED_IN && strcmp(table[i].name, name) == 0) {
            pthread_mutex_lock(&table[i].wlock);
            rc = net_write_line(table[i].fd, line);
            pthread_mutex_unlock(&table[i].wlock);
            break;
        }
    }
    pthread_mutex_unlock(&table_lock);
    return rc;
}

int session_list(char *out, size_t outsz)
{
    int n = 0;
    size_t len = 0;
    if (outsz == 0)
        return 0;
    out[0] = '\0';
    pthread_mutex_lock(&table_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (table[i].state != SLOT_LOGGED_IN)
            continue;
        size_t nl = strlen(table[i].name);
        if (len + nl + 2 > outsz)
            break;
        if (n > 0)
            out[len++] = ',';
        memcpy(out + len, table[i].name, nl);
        len += nl;
        out[len] = '\0';
        n++;
    }
    pthread_mutex_unlock(&table_lock);
    return n;
}

int session_is_online(const char *name)
{
    int online = 0;
    pthread_mutex_lock(&table_lock);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (table[i].state == SLOT_LOGGED_IN && strcmp(table[i].name, name) == 0)
            online = 1;
    pthread_mutex_unlock(&table_lock);
    return online;
}

int session_count(void)
{
    pthread_mutex_lock(&table_lock);
    int n = used_slots;
    pthread_mutex_unlock(&table_lock);
    return n;
}

int session_broadcast(const char *line)
{
    int n = 0;
    pthread_mutex_lock(&table_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (table[i].state != SLOT_LOGGED_IN)
            continue;
        pthread_mutex_lock(&table[i].wlock);
        if (net_write_line(table[i].fd, line) == 0)
            n++;
        pthread_mutex_unlock(&table[i].wlock);
    }
    pthread_mutex_unlock(&table_lock);
    return n;
}

void session_shutdown_all(void)
{
    pthread_mutex_lock(&table_lock);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (table[i].state != SLOT_FREE && table[i].fd >= 0)
            shutdown(table[i].fd, SHUT_RDWR);
    pthread_mutex_unlock(&table_lock);
}

int session_wait_empty(int seconds)
{
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += seconds;

    pthread_mutex_lock(&table_lock);
    while (used_slots > 0) {
        if (pthread_cond_timedwait(&table_empty, &table_lock, &deadline) == ETIMEDOUT)
            break;
    }
    int empty = (used_slots == 0);
    pthread_mutex_unlock(&table_lock);
    return empty;
}

void session_delivery_lock(void)   { pthread_mutex_lock(&delivery_lock); }
void session_delivery_unlock(void) { pthread_mutex_unlock(&delivery_lock); }
