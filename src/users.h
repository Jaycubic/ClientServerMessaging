/* users.h - the list of users allowed to log in (file based).
 *
 * File format: one username per line; blank lines and lines starting with '#'
 * are ignored. Loaded once at startup, read-only afterwards (thread safe).
 */
#ifndef USERS_H
#define USERS_H

/* Load the valid-user file. Returns number of users, or -1 on error. */
int  users_load(const char *path);

/* Is `name` in the valid-user list? */
int  users_is_valid(const char *name);

/* Is `name` syntactically acceptable ([A-Za-z0-9_.-], 1..MAX_USERNAME)? */
int  users_name_wellformed(const char *name);

int  users_count(void);

#endif
