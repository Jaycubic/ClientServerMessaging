/* session.h - table of client connections and logged-in users.
 *
 * Every accepted connection owns one slot from accept() until session_close().
 * A slot is CONNECTED until the client logs in, then LOGGED_IN (appears in the
 * online-user list). All table state is protected by one mutex; each session
 * additionally has a write lock so two threads never interleave bytes of two
 * lines on the same socket.
 *
 * Lock order (always acquire in this order):
 *     delivery lock  ->  table lock  ->  per-session write lock
 */
#ifndef SESSION_H
#define SESSION_H

#include <pthread.h>
#include <stddef.h>
#include "protocol.h"

typedef enum { SLOT_FREE = 0, SLOT_CONNECTED, SLOT_LOGGED_IN } slot_state_t;

typedef struct session {
    slot_state_t    state;
    int             fd;
    char            name[MAX_USERNAME + 1];
    char            addr[64];
    pthread_mutex_t wlock;
} session_t;

void session_table_init(void);

/* Claim a slot for a freshly accepted connection. NULL if the table is full. */
session_t *session_open(int fd, const char *addr);

/* Atomically claim `name` for this session. 0 = ok, -1 = already online. */
int session_login(session_t *s, const char *name);

/* Release the slot and close the socket. Safe to call once per session. */
void session_close(session_t *s);

/* Write one protocol line to this session (serialised by its write lock). */
int session_send(session_t *s, const char *line);

/* Write a line to the logged-in user `name`. 0 = written, -1 = not online
 * or write failed. */
int session_send_user(const char *name, const char *line);

/* Fill `out` with comma-separated online names. Returns number of users. */
int session_list(char *out, size_t outsz);

/* True if `name` is logged in. */
int session_is_online(const char *name);

/* Number of occupied slots (connected or logged in). */
int session_count(void);

/* Send `line` to every logged-in user. Returns number of recipients. */
int session_broadcast(const char *line);

/* shutdown(2) every socket so blocked reader threads wake up and exit. */
void session_shutdown_all(void);

/* Block until all slots are free or `seconds` elapse. Returns 1 if empty. */
int session_wait_empty(int seconds);

/* Serialises "is user online? + store message" against "login + fetch unread"
 * so a message can never fall between the two and be missed. */
void session_delivery_lock(void);
void session_delivery_unlock(void);

#endif
