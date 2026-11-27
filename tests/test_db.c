#define _GNU_SOURCE
#include <pthread.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "db.h"
#include "log.h"
#include "test_util.h"

static char dir[64];
static char path[128];

static void new_path(void)
{
    static int counter;
    snprintf(path, sizeof path, "%s/db%d.dat", dir, counter++);
}

static int count_lines(const char *p)
{
    FILE *f = fopen(p, "r");
    int n = 0, c;
    if (!f) return -1;
    while ((c = fgetc(f)) != EOF) if (c == '\n') n++;
    fclose(f);
    return n;
}

static int history_count(const char *a, const char *b)
{
    db_msg_t *m; int n = -1;
    db_history(a, b, &m, &n);
    db_free(m);
    return n;
}

static void test_memory_mode_semantics(void)
{
    CHECK(db_init(NULL) == 0);
    CHECK(db_save("alice", "bob", "hi", 0) == 0);
    CHECK(db_save("alice", "bob", "live", 1) == 0);       /* already delivered */
    CHECK(db_save("carol", "bob", "other", 0) == 0);

    db_msg_t *m; int n;
    CHECK(db_take_unread("bob", &m, &n) == 0);
    CHECK(n == 2);
    CHECK(strcmp(m[0].text, "hi") == 0 && strcmp(m[1].text, "other") == 0);  /* oldest first */
    db_free(m);
    CHECK(db_take_unread("bob", &m, &n) == 0 && n == 0);                      /* only once */
    db_free(m);
    CHECK(db_take_unread("alice", &m, &n) == 0 && n == 0);
    db_free(m);

    CHECK(history_count("alice", "bob") == 2);
    CHECK(history_count("bob", "alice") == 2);            /* symmetric */
    CHECK(history_count("alice", "carol") == 0);
    CHECK(db_delete("alice", "bob") == 2);
    CHECK(history_count("alice", "bob") == 0);
    CHECK(history_count("carol", "bob") == 1);            /* others untouched */
    CHECK(db_delete("alice", "bob") == 0);
    db_close();
}

static void test_persistence_across_restart(void)
{
    new_path();
    CHECK(db_init(path) == 0);
    db_save("alice", "bob", "first", 0);
    db_save("bob", "alice", "second", 1);
    db_save("alice", "bob", "third", 0);
    db_close();

    CHECK(db_init(path) == 0);                            /* "restart" */
    db_msg_t *m; int n;
    CHECK(db_history("alice", "bob", &m, &n) == 0 && n == 3);
    CHECK(strcmp(m[0].text, "first") == 0 && strcmp(m[2].text, "third") == 0);
    CHECK(strcmp(m[1].from, "bob") == 0 && strcmp(m[1].to, "alice") == 0);
    CHECK(m[0].ts > 1000000000);
    db_free(m);
    CHECK(db_take_unread("bob", &m, &n) == 0 && n == 2);  /* unread state kept */
    db_free(m);
    db_close();
}

static void test_delivered_flag_persists(void)
{
    new_path();
    db_init(path);
    db_save("alice", "bob", "x", 0);
    db_msg_t *m; int n;
    db_take_unread("bob", &m, &n); db_free(m);
    CHECK(n == 1);
    db_close();

    db_init(path);
    db_take_unread("bob", &m, &n); db_free(m);
    CHECK(n == 0);                                        /* not delivered twice */
    db_close();
}

static void test_delete_persists_and_compacts(void)
{
    new_path();
    db_init(path);
    for (int i = 0; i < 10; i++) db_save("alice", "bob", "m", 1);
    db_save("carol", "bob", "keep", 1);
    CHECK(count_lines(path) == 11);
    CHECK(db_delete("bob", "alice") == 10);
    CHECK(count_lines(path) == 1);                        /* file rewritten */
    db_save("alice", "bob", "after", 1);                  /* append handle still valid */
    db_close();

    db_init(path);
    CHECK(history_count("alice", "bob") == 1);
    CHECK(history_count("carol", "bob") == 1);
    db_close();
}

static void test_special_characters_roundtrip(void)
{
    new_path();
    const char *texts[] = {
        "tab\there", "back\\slash", "literal \\n not newline", "pipes | a|b|c",
        "unicode: caf\xc3\xa9 \xe2\x9c\x93", "trailing backslash\\", "  spaces  ",
        "cr\rinside", "ends with tab\t",
    };
    int nt = (int)(sizeof texts / sizeof *texts);
    db_init(path);
    for (int i = 0; i < nt; i++) db_save("alice", "bob", texts[i], 1);
    db_close();
    db_init(path);
    db_msg_t *m; int n;
    db_history("alice", "bob", &m, &n);
    CHECK(n == nt);
    for (int i = 0; i < n && i < nt; i++) CHECK(strcmp(m[i].text, texts[i]) == 0);
    db_free(m);
    db_close();
    CHECK(count_lines(path) == nt);                       /* one line per record */
}

static void test_ids_continue_after_restart(void)
{
    new_path();
    db_init(path);
    db_save("alice", "bob", "a", 0);
    db_save("alice", "bob", "b", 0);
    db_close();
    db_init(path);
    db_save("alice", "bob", "c", 0);
    db_delete("alice", "carol");                          /* no-op */
    db_close();
    db_init(path);
    CHECK(history_count("alice", "bob") == 3);
    db_close();
}

static void test_corrupt_and_torn_lines_skipped(void)
{
    new_path();
    FILE *f = fopen(path, "w");
    fputs("M\t1\t1700000000\t1\talice\tbob\tgood one\n", f);
    fputs("garbage line without tabs\n", f);
    fputs("M\tnotanumber\t1\t1\talice\tbob\tbad id\n", f);
    fputs("M\t5\t1700000001\t7\talice\tbob\tbad flag\n", f);
    fputs("M\t2\t1700000002\t0\tcarol\tbob\tunread one\n", f);
    fputs("V\t999\n", f);                                 /* refers to nothing */
    fputs("M\t3\t1700000003\t1\talice\tbob\ttorn fin", f);/* no newline: crash mid-write */
    fclose(f);

    CHECK(db_init(path) == 0);
    CHECK(history_count("alice", "bob") == 1);
    db_msg_t *m; int n;
    db_take_unread("bob", &m, &n);
    CHECK(n == 1 && strcmp(m[0].text, "unread one") == 0);
    db_free(m);
    CHECK(db_save("alice", "bob", "new", 1) == 0);
    db_close();
    db_init(path);
    CHECK(history_count("alice", "bob") == 2);
    db_close();
}

static void test_file_permissions_private(void)
{
    new_path();
    db_init(path);
    db_save("alice", "bob", "secret", 1);
    db_close();
    struct stat st;
    CHECK(stat(path, &st) == 0);
    CHECK((st.st_mode & 077) == 0);                       /* no group/other access */
}

static void test_limits_and_errors(void)
{
    db_init(NULL);
    char big[MAX_TEXT + 10];
    memset(big, 'x', sizeof big - 1); big[sizeof big - 1] = '\0';
    CHECK(db_save("alice", "bob", big, 0) == -1);         /* too long rejected */
    char exact[MAX_TEXT + 1];
    memset(exact, 'y', MAX_TEXT); exact[MAX_TEXT] = '\0';
    CHECK(db_save("alice", "bob", exact, 0) == 0);
    db_close();
    CHECK(db_init("/nonexistent-dir/x.dat") == -1);       /* unwritable path */
}

#define WRITERS 8
#define PER_WRITER 100
static void *writer(void *arg)
{
    long id = (long)arg;
    char text[32];
    for (int i = 0; i < PER_WRITER; i++) {
        snprintf(text, sizeof text, "w%ld-%d", id, i);
        db_save("alice", "bob", text, 0);
    }
    return NULL;
}

static void test_concurrent_writes_all_persisted(void)
{
    new_path();
    db_init(path);
    pthread_t th[WRITERS];
    for (long i = 0; i < WRITERS; i++) pthread_create(&th[i], NULL, writer, (void *)i);
    for (int i = 0; i < WRITERS; i++) pthread_join(th[i], NULL);
    CHECK(history_count("alice", "bob") == WRITERS * PER_WRITER);
    db_close();
    db_init(path);
    CHECK(history_count("alice", "bob") == WRITERS * PER_WRITER);
    CHECK(count_lines(path) == WRITERS * PER_WRITER);
    db_close();
}

static void *reader_deleter(void *arg)
{
    (void)arg;
    for (int i = 0; i < 50; i++) {
        db_msg_t *m; int n;
        db_history("alice", "bob", &m, &n);
        db_free(m);
        db_take_unread("bob", &m, &n);
        db_free(m);
        if (i % 10 == 0) db_delete("alice", "bob");
    }
    return NULL;
}

static void test_concurrent_mixed_operations(void)
{
    new_path();
    db_init(path);
    pthread_t w[4], r[2];
    for (long i = 0; i < 4; i++) pthread_create(&w[i], NULL, writer, (void *)i);
    for (int i = 0; i < 2; i++) pthread_create(&r[i], NULL, reader_deleter, NULL);
    for (int i = 0; i < 4; i++) pthread_join(w[i], NULL);
    for (int i = 0; i < 2; i++) pthread_join(r[i], NULL);
    int before = history_count("alice", "bob");
    db_close();
    db_init(path);
    CHECK(history_count("alice", "bob") == before);       /* disk == memory */
    db_close();
}

int main(void)
{
    log_set_quiet(1);
    snprintf(dir, sizeof dir, "/tmp/dbtest.XXXXXX");
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 2; }

    RUN(test_memory_mode_semantics);
    RUN(test_persistence_across_restart);
    RUN(test_delivered_flag_persists);
    RUN(test_delete_persists_and_compacts);
    RUN(test_special_characters_roundtrip);
    RUN(test_ids_continue_after_restart);
    RUN(test_corrupt_and_torn_lines_skipped);
    RUN(test_file_permissions_private);
    RUN(test_limits_and_errors);
    RUN(test_concurrent_writes_all_persisted);
    RUN(test_concurrent_mixed_operations);

    char cmd[160];
    snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    if (system(cmd)) {}
    TEST_DONE("test_db");
}
