#include "log.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static int log_quiet = 0;

void log_set_quiet(int quiet)
{
    pthread_mutex_lock(&log_lock);
    log_quiet = quiet;
    pthread_mutex_unlock(&log_lock);
}

void log_event(const char *fmt, ...)
{
    char stamp[16];
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    strftime(stamp, sizeof stamp, "%H:%M:%S", &tmv);

    va_list ap;
    va_start(ap, fmt);
    pthread_mutex_lock(&log_lock);
    if (!log_quiet) {
        printf("[%s] ", stamp);
        vprintf(fmt, ap);
        printf("\n");
        fflush(stdout);
    }
    pthread_mutex_unlock(&log_lock);
    va_end(ap);
}
