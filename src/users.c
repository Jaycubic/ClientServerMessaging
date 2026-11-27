#include "users.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "protocol.h"

#define MAX_USERS 256

static char names[MAX_USERS][MAX_USERNAME + 1];
static int  nusers;

int users_name_wellformed(const char *name)
{
    size_t n = strlen(name);
    if (n == 0 || n > MAX_USERNAME)
        return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        if (!(isalnum(c) || c == '_' || c == '-' || c == '.'))
            return 0;
    }
    return 1;
}

int users_load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    nusers = 0;
    char line[256];
    while (fgets(line, sizeof line, f) && nusers < MAX_USERS) {
        line[strcspn(line, "\r\n")] = '\0';
        char *p = line;
        while (isspace((unsigned char)*p))
            p++;
        size_t n = strlen(p);
        while (n > 0 && isspace((unsigned char)p[n - 1]))
            p[--n] = '\0';
        if (n == 0 || *p == '#' || !users_name_wellformed(p))
            continue;
        snprintf(names[nusers++], sizeof names[0], "%s", p);
    }
    fclose(f);
    return nusers;
}

int users_is_valid(const char *name)
{
    for (int i = 0; i < nusers; i++)
        if (strcmp(names[i], name) == 0)
            return 1;
    return 0;
}

int users_count(void)
{
    return nusers;
}
