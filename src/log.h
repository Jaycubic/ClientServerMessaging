/* log.h - thread-safe timestamped console logging for server events. */
#ifndef LOG_H
#define LOG_H

void log_event(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_set_quiet(int quiet);   /* suppress output (used by unit tests) */

#endif
