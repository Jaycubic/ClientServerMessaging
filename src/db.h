/* db.h - server-side message storage interface.
 *
 * Implemented by db_stub.c (in-memory, used for development/tests) and to be
 * replaced by the persistent implementation (SQLite / flat file). Every
 * function must be thread safe: the server calls them from many threads.
 */
#ifndef DB_H
#define DB_H

#include <time.h>
#include "protocol.h"

typedef struct {
    char   from[MAX_USERNAME + 1];
    char   to[MAX_USERNAME + 1];
    time_t ts;
    char   text[MAX_TEXT + 1];
} db_msg_t;

int  db_init(const char *path);   /* 0 ok, -1 error */
void db_close(void);

/* Store a message. delivered=1 if it was already pushed to an online user. */
int  db_save(const char *from, const char *to, const char *text, int delivered);

/* Fetch (and mark delivered) all undelivered messages for `user`, oldest
 * first. On success *out is malloc'd (free with db_free), *n may be 0. */
int  db_take_unread(const char *user, db_msg_t **out, int *n);

/* All messages exchanged between a and b, in both directions, oldest first. */
int  db_history(const char *a, const char *b, db_msg_t **out, int *n);

/* Delete every message between a and b. Returns number deleted, or -1. */
int  db_delete(const char *a, const char *b);

void db_free(db_msg_t *msgs);

#endif
