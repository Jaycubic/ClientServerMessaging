/* db_file.c - persistent flat-file implementation of db.h.
 *
 * Storage format: a text log, one record per line, TAB separated:
 *
 *     M <id> <unix-ts> <delivered 0|1> <from> <to> <escaped-text>   message
 *     V <id>                                                        now delivered
 *
 * Text is escaped so a record never spans lines:  \ -> \\   LF -> \n
 * CR -> \r   TAB -> \t.  Usernames are restricted to [A-Za-z0-9_.-] so they
 * never need escaping.
 *
 * All messages are also kept in an in-memory array (the working set). Every
 * mutation is appended to the log and fsync()ed *before* it is acknowledged,
 * so an acknowledged message survives a crash. A delete rewrites the whole
 * file to a temporary name and rename()s it over the old one (atomic), so a
 * crash mid-delete leaves either the old or the new file, never a torn one.
 * At startup the log is replayed, torn/garbage lines are skipped, and the file
 * is compacted.
 *
 * Passing path == NULL to db_init() gives a purely in-memory store (tests).
 * A single mutex serialises everything: simple and obviously race-free; the
 * cost is that saves are serialised, which is fine at this scale.
 */
#define _GNU_SOURCE
#include "db.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "log.h"

typedef struct {
    unsigned long id;
    db_msg_t      m;
    int           delivered;
} rec_t;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static rec_t   *recs;
static size_t   nrecs, caprecs;
static unsigned long next_id = 1;
static char    *file_path;          /* NULL = memory-only */
static FILE    *logf;               /* append handle on file_path */

/* ---------- escaping ---------- */

static size_t escape(const char *in, char *out, size_t outsz)
{
    size_t n = 0;
    for (; *in && n + 2 < outsz; in++) {
        switch (*in) {
        case '\\': out[n++] = '\\'; out[n++] = '\\'; break;
        case '\n': out[n++] = '\\'; out[n++] = 'n';  break;
        case '\r': out[n++] = '\\'; out[n++] = 'r';  break;
        case '\t': out[n++] = '\\'; out[n++] = 't';  break;
        default:   out[n++] = *in;
        }
    }
    out[n] = '\0';
    return n;
}

static void unescape(const char *in, char *out, size_t outsz)
{
    size_t n = 0;
    for (; *in && n + 1 < outsz; in++) {
        if (*in == '\\' && in[1]) {
            in++;
            switch (*in) {
            case 'n': out[n++] = '\n'; break;
            case 'r': out[n++] = '\r'; break;
            case 't': out[n++] = '\t'; break;
            default:  out[n++] = *in;          /* includes '\\' */
            }
        } else {
            out[n++] = *in;
        }
    }
    out[n] = '\0';
}

/* ---------- file helpers ---------- */

static int write_rec(FILE *f, const rec_t *r)
{
    char esc[2 * MAX_TEXT + 8];
    escape(r->m.text, esc, sizeof esc);
    return fprintf(f, "M\t%lu\t%ld\t%d\t%s\t%s\t%s\n", r->id, (long)r->m.ts,
                   r->delivered, r->m.from, r->m.to, esc) < 0 ? -1 : 0;
}

static int sync_file(FILE *f)
{
    if (fflush(f) != 0)
        return -1;
    return fsync(fileno(f));
}

/* Rewrite the log from `rs[0..n)` atomically. Returns 0 on success. */
static int write_snapshot(const char *path, const rec_t *rs, size_t n)
{
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return -1;
    FILE *f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        return -1;
    }
    int rc = 0;
    for (size_t i = 0; i < n && rc == 0; i++)
        rc = write_rec(f, &rs[i]);
    if (rc == 0)
        rc = sync_file(f);
    if (fclose(f) != 0)
        rc = -1;
    if (rc == 0 && rename(tmp, path) != 0)
        rc = -1;
    if (rc != 0)
        unlink(tmp);
    return rc;
}

static int append_line(const char *fmt, unsigned long id)
{
    if (!logf)
        return 0;
    if (fprintf(logf, fmt, id) < 0)
        return -1;
    return sync_file(logf);
}

/* ---------- in-memory array ---------- */

static int reserve(size_t want)
{
    if (want <= caprecs)
        return 0;
    size_t ncap = caprecs ? caprecs * 2 : 64;
    while (ncap < want)
        ncap *= 2;
    rec_t *p = realloc(recs, ncap * sizeof *p);
    if (!p)
        return -1;
    recs = p;
    caprecs = ncap;
    return 0;
}

static void reset_state(void)
{
    if (logf)
        fclose(logf);
    logf = NULL;
    free(file_path);
    file_path = NULL;
    free(recs);
    recs = NULL;
    nrecs = caprecs = 0;
    next_id = 1;
}

/* Parse one log line (modified in place). Returns 0 if applied/ignored
 * harmlessly, -1 if the line is malformed. */
static int replay_line(char *line)
{
    char *f[7];
    int nf = 0;
    char *p = line;
    while (nf < 6) {                     /* first six TAB-separated fields */
        f[nf++] = p;
        char *t = strchr(p, '\t');
        if (!t)
            break;
        *t = '\0';
        p = t + 1;
        if (nf == 6)
            f[nf++] = p;                 /* the rest = escaped text */
    }

    if (nf >= 2 && strcmp(f[0], "V") == 0) {
        unsigned long id = strtoul(f[1], NULL, 10);
        for (size_t i = 0; i < nrecs; i++)
            if (recs[i].id == id)
                recs[i].delivered = 1;
        return 0;
    }
    if (nf != 7 || strcmp(f[0], "M") != 0)
        return -1;

    char *end;
    unsigned long id = strtoul(f[1], &end, 10);
    if (*end || id == 0)
        return -1;
    long ts = strtol(f[2], &end, 10);
    if (*end)
        return -1;
    if ((f[3][0] != '0' && f[3][0] != '1') || f[3][1])
        return -1;
    if (!*f[4] || !*f[5] || strlen(f[4]) > MAX_USERNAME || strlen(f[5]) > MAX_USERNAME)
        return -1;
    if (reserve(nrecs + 1) != 0)
        return -1;

    rec_t *r = &recs[nrecs++];
    memset(r, 0, sizeof *r);
    r->id = id;
    r->m.ts = (time_t)ts;
    r->delivered = f[3][0] == '1';
    snprintf(r->m.from, sizeof r->m.from, "%s", f[4]);
    snprintf(r->m.to, sizeof r->m.to, "%s", f[5]);
    unescape(f[6], r->m.text, sizeof r->m.text);
    if (id >= next_id)
        next_id = id + 1;
    return 0;
}

static int load_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return errno == ENOENT ? 0 : -1;
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    int bad = 0;
    while ((len = getline(&line, &cap, f)) > 0) {
        if (line[len - 1] != '\n') {     /* torn final line from a crash */
            bad++;
            break;
        }
        line[len - 1] = '\0';
        if (line[0] && replay_line(line) != 0)
            bad++;
    }
    free(line);
    fclose(f);
    if (bad)
        log_event("storage: skipped %d malformed line(s) in %s", bad, path);
    return 0;
}

/* ---------- public API ---------- */

int db_init(const char *path)
{
    pthread_mutex_lock(&lock);
    reset_state();
    int rc = 0;
    if (path) {
        file_path = strdup(path);
        if (!file_path || load_file(path) != 0 ||
            write_snapshot(path, recs, nrecs) != 0 ||      /* compact */
            !(logf = fopen(path, "a"))) {
            reset_state();
            rc = -1;
        }
    }
    pthread_mutex_unlock(&lock);
    return rc;
}

void db_close(void)
{
    pthread_mutex_lock(&lock);
    if (logf)
        sync_file(logf);
    reset_state();
    pthread_mutex_unlock(&lock);
}

int db_save(const char *from, const char *to, const char *text, int delivered)
{
    if (strlen(from) > MAX_USERNAME || strlen(to) > MAX_USERNAME ||
        strlen(text) > MAX_TEXT)
        return -1;

    pthread_mutex_lock(&lock);
    int rc = -1;
    if (reserve(nrecs + 1) == 0) {
        rec_t *r = &recs[nrecs];
        memset(r, 0, sizeof *r);
        r->id = next_id;
        r->m.ts = time(NULL);
        r->delivered = delivered ? 1 : 0;
        snprintf(r->m.from, sizeof r->m.from, "%s", from);
        snprintf(r->m.to, sizeof r->m.to, "%s", to);
        snprintf(r->m.text, sizeof r->m.text, "%s", text);

        int ok = 0;
        if (logf)
            ok = (write_rec(logf, r) == 0 && sync_file(logf) == 0);
        else
            ok = 1;
        if (ok) {                        /* acknowledge only once durable */
            nrecs++;
            next_id++;
            rc = 0;
        }
    }
    pthread_mutex_unlock(&lock);
    return rc;
}

int db_take_unread(const char *user, db_msg_t **out, int *n)
{
    *out = NULL;
    *n = 0;
    pthread_mutex_lock(&lock);
    db_msg_t *res = malloc((nrecs ? nrecs : 1) * sizeof *res);
    if (!res) {
        pthread_mutex_unlock(&lock);
        return -1;
    }
    int cnt = 0;
    for (size_t i = 0; i < nrecs; i++) {
        if (!recs[i].delivered && strcmp(recs[i].m.to, user) == 0) {
            res[cnt++] = recs[i].m;
            recs[i].delivered = 1;
            /* If this append fails the worst case is a re-delivery after a
             * restart (at-least-once), never a lost message. */
            if (append_line("V\t%lu\n", recs[i].id) != 0)
                log_event("storage: could not persist delivered flag");
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
    db_msg_t *res = malloc((nrecs ? nrecs : 1) * sizeof *res);
    if (!res) {
        pthread_mutex_unlock(&lock);
        return -1;
    }
    int cnt = 0;
    for (size_t i = 0; i < nrecs; i++)
        if (between(&recs[i], a, b))
            res[cnt++] = recs[i].m;
    pthread_mutex_unlock(&lock);
    *out = res;
    *n = cnt;
    return 0;
}

int db_delete(const char *a, const char *b)
{
    pthread_mutex_lock(&lock);
    size_t removed = 0;
    for (size_t i = 0; i < nrecs; i++)
        if (between(&recs[i], a, b))
            removed++;
    if (removed == 0) {
        pthread_mutex_unlock(&lock);
        return 0;
    }

    /* Build the surviving set, persist it atomically, then swap it in. */
    rec_t *keep = malloc((nrecs - removed ? nrecs - removed : 1) * sizeof *keep);
    if (!keep) {
        pthread_mutex_unlock(&lock);
        return -1;
    }
    size_t k = 0;
    for (size_t i = 0; i < nrecs; i++)
        if (!between(&recs[i], a, b))
            keep[k++] = recs[i];

    int rc = 0;
    if (file_path) {
        rc = write_snapshot(file_path, keep, k);
        if (rc == 0) {                   /* reopen the append handle on the new file */
            fclose(logf);
            logf = fopen(file_path, "a");
            if (!logf)
                rc = -1;
        }
    }
    if (rc != 0) {
        free(keep);
        pthread_mutex_unlock(&lock);
        return -1;
    }
    memcpy(recs, keep, k * sizeof *keep);
    nrecs = k;
    free(keep);
    pthread_mutex_unlock(&lock);
    return (int)removed;
}

void db_free(db_msg_t *msgs)
{
    free(msgs);
}
