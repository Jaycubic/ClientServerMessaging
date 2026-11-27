#define _GNU_SOURCE
#include "cli.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

int cli_name_ok(const char *name)
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

/* Copy the next whitespace-delimited token of *p into tok, advance *p past it.
 * Returns 1 if a token was found. */
static int next_token(const char **p, char *tok, size_t toksz)
{
    const char *s = *p;
    while (isspace((unsigned char)*s))
        s++;
    if (*s == '\0')
        return 0;
    size_t n = 0;
    while (*s && !isspace((unsigned char)*s)) {
        if (n + 1 < toksz)
            tok[n++] = *s;
        s++;
    }
    tok[n] = '\0';
    *p = s;
    return 1;
}

static void fail(cli_result_t *res, const char *msg)
{
    res->kind = CLI_ERROR;
    snprintf(res->error, sizeof res->error, "%s", msg);
}

static void parse_connect(const char *args, cli_result_t *res)
{
    char host[256], user[64], port[16];
    if (!next_token(&args, host, sizeof host) || !next_token(&args, user, sizeof user)) {
        fail(res, "Usage: connect <server IP> <user name> [port]");
        return;
    }
    if (!cli_name_ok(user)) {
        fail(res, "Invalid user name (use letters, digits, _ - . ; max 32 characters)");
        return;
    }
    res->port = 0;
    if (next_token(&args, port, sizeof port)) {
        char *end;
        long p = strtol(port, &end, 10);
        if (*end || p < 1 || p > 65535) {
            fail(res, "Invalid port number");
            return;
        }
        res->port = (int)p;
    }
    snprintf(res->host, sizeof res->host, "%s", host);
    snprintf(res->user, sizeof res->user, "%.*s", MAX_USERNAME, user);
    res->kind = CLI_CONNECT;
}

static void parse_sendmessage(const char *args, cli_result_t *res)
{
    char user[64];
    if (!next_token(&args, user, sizeof user)) {
        fail(res, "Usage: sendmessage <user> <message>");
        return;
    }
    while (isspace((unsigned char)*args))
        args++;
    size_t len = strlen(args);
    while (len > 0 && isspace((unsigned char)args[len - 1]))
        len--;
    if (len == 0) {
        fail(res, "Usage: sendmessage <user> <message>");
        return;
    }
    if (!cli_name_ok(user)) {
        fail(res, "Invalid user name");
        return;
    }
    if (len > MAX_TEXT) {
        snprintf(res->error, sizeof res->error,
                 "Message too long (%zu characters, max %d)", len, MAX_TEXT);
        res->kind = CLI_ERROR;
        return;
    }
    snprintf(res->line, sizeof res->line, "%s|%s|%.*s", CMD_SEND, user, (int)len, args);
    res->kind = CLI_SEND;
}

static void parse_one_user(const char *args, const char *proto, const char *usage,
                           cli_result_t *res)
{
    char user[64], extra[8];
    if (!next_token(&args, user, sizeof user) || next_token(&args, extra, sizeof extra)) {
        fail(res, usage);
        return;
    }
    if (!cli_name_ok(user)) {
        fail(res, "Invalid user name");
        return;
    }
    snprintf(res->line, sizeof res->line, "%s|%s", proto, user);
    res->kind = CLI_SEND;
}

void cli_parse(const char *input, int connected, cli_result_t *res)
{
    memset(res, 0, sizeof *res);
    res->kind = CLI_EMPTY;

    char cmd[64];
    const char *args = input;
    if (!next_token(&args, cmd, sizeof cmd))
        return;

    if (strcasecmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        res->kind = CLI_HELP;
    } else if (strcasecmp(cmd, "shutdown") == 0) {
        res->kind = CLI_SHUTDOWN;
    } else if (strcasecmp(cmd, "connect") == 0) {
        if (connected)
            fail(res, "Already connected. Type 'shutdown' to disconnect first.");
        else
            parse_connect(args, res);
    } else if (!connected) {
        fail(res, "Not connected. Use: connect <server IP> <user name>");
    } else if (strcasecmp(cmd, "sendmessage") == 0) {
        parse_sendmessage(args, res);
    } else if (strcasecmp(cmd, "getmessages") == 0) {
        parse_one_user(args, CMD_GETMSGS, "Usage: getmessages <user>", res);
    } else if (strcasecmp(cmd, "deletemessages") == 0) {
        parse_one_user(args, CMD_DELMSGS, "Usage: deletemessages <user>", res);
    } else if (strcasecmp(cmd, "getuserlist") == 0) {
        snprintf(res->line, sizeof res->line, "%s", CMD_USERS);
        res->kind = CLI_SEND;
    } else {
        snprintf(res->error, sizeof res->error,
                 "Unknown command '%.40s'. Type 'help' for the list of commands.", cmd);
        res->kind = CLI_ERROR;
    }
}

const char *cli_help_text(void)
{
    return
        "Commands:\n"
        "  connect <server IP> <user name> [port]   log in to the server\n"
        "  sendmessage <user> <message>             send a message\n"
        "  getmessages <user>                       show history with a user\n"
        "  deletemessages <user>                    delete history with a user\n"
        "  getuserlist                              list users who are online\n"
        "  shutdown                                 disconnect and exit\n"
        "  help                                     show this text";
}

/* Split off the next '|'-separated field of *p (NUL-terminates it in place). */
static char *field(char **p)
{
    char *start = *p;
    if (!start)
        return NULL;
    char *bar = strchr(start, '|');
    if (bar) {
        *bar = '\0';
        *p = bar + 1;
    } else {
        *p = NULL;
    }
    return start;
}

static void fmt_time(const char *ts, char *out, size_t outsz)
{
    time_t t = (time_t)strtol(ts, NULL, 10);
    struct tm tmv;
    if (!localtime_r(&t, &tmv) || strftime(out, outsz, "%Y-%m-%d %H:%M:%S", &tmv) == 0)
        snprintf(out, outsz, "%s", ts);
}

cli_fmt_t cli_format(const char *server_line, char *out, size_t outsz)
{
    char buf[MAX_LINE + 1];
    snprintf(buf, sizeof buf, "%s", server_line);
    char *rest = buf;
    char *kind = field(&rest);
    if (!kind)
        kind = "";

    if (strcmp(kind, RSP_MSG) == 0) {
        char *from = field(&rest);
        snprintf(out, outsz, "Message from %s: %s", from ? from : "?", rest ? rest : "");
    } else if (strcmp(kind, RSP_UNREAD) == 0) {
        char *from = field(&rest), *ts = field(&rest), when[40] = "";
        if (ts)
            fmt_time(ts, when, sizeof when);
        snprintf(out, outsz, "[unread %s] Message from %s: %s", when,
                 from ? from : "?", rest ? rest : "");
    } else if (strcmp(kind, RSP_HIST) == 0) {
        char *from = field(&rest), *to = field(&rest), *ts = field(&rest), when[40] = "";
        if (ts)
            fmt_time(ts, when, sizeof when);
        snprintf(out, outsz, "[%s] %s -> %s: %s", when, from ? from : "?",
                 to ? to : "?", rest ? rest : "");
    } else if (strcmp(kind, RSP_USERLIST) == 0) {
        if (!rest || *rest == '\0') {
            snprintf(out, outsz, "No users online");
        } else {
            int n = 1;
            for (const char *c = rest; *c; c++)
                if (*c == ',')
                    n++;
            size_t pos = (size_t)snprintf(out, outsz, "Online users (%d): ", n);
            for (const char *c = rest; *c && pos + 2 < outsz; c++) {
                if (*c == ',') {
                    out[pos++] = ',';
                    out[pos++] = ' ';
                } else {
                    out[pos++] = *c;
                }
            }
            out[pos < outsz ? pos : outsz - 1] = '\0';
        }
    } else if (strcmp(kind, RSP_ERR) == 0) {
        snprintf(out, outsz, "Error: %s", rest ? rest : "");
    } else if (strcmp(kind, RSP_OK) == 0) {
        snprintf(out, outsz, "%s", rest ? rest : "");
    } else if (strcmp(kind, RSP_SHUTDOWN) == 0) {
        snprintf(out, outsz, "%s", rest ? rest : "Server shutting down...");
        return FMT_SHUTDOWN;
    } else {
        snprintf(out, outsz, "%s", server_line);       /* unknown: show raw */
    }
    return FMT_TEXT;
}
