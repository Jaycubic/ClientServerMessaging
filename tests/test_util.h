/* Minimal assertion helpers for the C unit tests. */
#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdio.h>
#include <stdlib.h>

static int t_failures = 0;
static int t_checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        t_checks++;                                                          \
        if (!(cond)) {                                                       \
            t_failures++;                                                    \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                    \
    } while (0)

#define RUN(fn)                                                              \
    do {                                                                     \
        int before = t_failures;                                             \
        fn();                                                                \
        printf("  %-40s %s\n", #fn, t_failures == before ? "ok" : "FAILED"); \
    } while (0)

#define TEST_DONE(name)                                                      \
    do {                                                                     \
        printf("%s: %d checks, %d failures\n", name, t_checks, t_failures);  \
        return t_failures ? 1 : 0;                                           \
    } while (0)

#endif
