/* db_stub.c - in-memory implementation of db.h.
 *
 * Placeholder so the server runs end to end; the persistent (SQLite / flat
 * file) implementation replaces this file and must keep the same semantics.
 */
#include "db.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    db_msg_t m;
    int      delivered;
} rec_t;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static rec_t *recs;
static int    nrecs, caprecs;

int db_init(const char *path)
{
    (void)path;
    pthread_mutex_lock(&lock);
    free(recs);
    recs = NULL;
    nrecs = caprecs = 0;
    pthread_mutex_unlock(&lock);
    return 0;
}

void db_close(void)
{
    db_init(NULL);
}

int db_save(const char *from, const char *to, const char *text, int delivered)
{
    int rc = 0;
    pthread_mutex_lock(&lock);
    if (nrecs == caprecs) {
        int ncap = caprecs ? caprecs * 2 : 64;
        rec_t *p = realloc(recs, (size_t)ncap * sizeof *p);
        if (!p) {
            rc = -1;
            goto out;
        }
        recs = p;
        caprecs = ncap;
    }
    rec_t *r = &recs[nrecs++];
    memset(r, 0, sizeof *r);
    snprintf(r->m.from, sizeof r->m.from, "%s", from);
    snprintf(r->m.to, sizeof r->m.to, "%s", to);
    snprintf(r->m.text, sizeof r->m.text, "%s", text);
    r->m.ts = time(NULL);
    r->delivered = delivered;
out:
    pthread_mutex_unlock(&lock);
    return rc;
}

int db_take_unread(const char *user, db_msg_t **out, int *n)
{
    *out = NULL;
    *n = 0;
    pthread_mutex_lock(&lock);
    db_msg_t *res = malloc((size_t)(nrecs ? nrecs : 1) * sizeof *res);
    if (!res) {
        pthread_mutex_unlock(&lock);
        return -1;
    }
    int cnt = 0;
    for (int i = 0; i < nrecs; i++) {
        if (!recs[i].delivered && strcmp(recs[i].m.to, user) == 0) {
            res[cnt++] = recs[i].m;
            recs[i].delivered = 1;
        }
    }
    pthread_mutex_unlock(&lock);
    *out = res;
    *n = cnt;
    return 0;
}

static int between(const rec_t *r, const char *a, const char *b)
{
    return (!strcmp(r->m.from, a) && !strcmp(r->m.to, b)) ||
           (!strcmp(r->m.from, b) && !strcmp(r->m.to, a));
}

int db_history(const char *a, const char *b, db_msg_t **out, int *n)
{
    *out = NULL;
    *n = 0;
    pthread_mutex_lock(&lock);
    db_msg_t *res = malloc((size_t)(nrecs ? nrecs : 1) * sizeof *res);
    if (!res) {
        pthread_mutex_unlock(&lock);
        return -1;
    }
    int cnt = 0;
    for (int i = 0; i < nrecs; i++)
        if (between(&recs[i], a, b))
            res[cnt++] = recs[i].m;
    pthread_mutex_unlock(&lock);
    *out = res;
    *n = cnt;
    return 0;
}

int db_delete(const char *a, const char *b)
{
    int removed = 0;
    pthread_mutex_lock(&lock);
    int w = 0;
    for (int i = 0; i < nrecs; i++) {
        if (between(&recs[i], a, b))
            removed++;
        else
            recs[w++] = recs[i];
    }
    nrecs = w;
    pthread_mutex_unlock(&lock);
    return removed;
}

void db_free(db_msg_t *msgs)
{
    free(msgs);
}
