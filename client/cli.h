/* cli.h - pure (no I/O) helpers for the command-line client:
 *   cli_parse()  : user input line  -> protocol line / action
 *   cli_format() : server line      -> human readable text
 * Keeping them free of sockets and terminals makes them unit-testable.
 */
#ifndef CLI_H
#define CLI_H

#include <stddef.h>
#include "protocol.h"

typedef enum {
    CLI_EMPTY,      /* blank input, nothing to do                        */
    CLI_SEND,       /* res->line holds a protocol line to send           */
    CLI_CONNECT,    /* res->host / user / port describe the connection   */
    CLI_SHUTDOWN,   /* disconnect from the server and exit the client    */
    CLI_HELP,       /* print help text                                   */
    CLI_ERROR       /* res->error holds a message for the user           */
} cli_kind_t;

typedef struct {
    cli_kind_t kind;
    char       line[MAX_LINE + 1];
    char       host[256];
    char       user[MAX_USERNAME + 1];
    int        port;               /* 0 = use the default port */
    char       error[200];
} cli_result_t;

/* Interpret one line typed by the user. `connected` selects which commands
 * are legal (before login only connect/help/shutdown). */
void cli_parse(const char *input, int connected, cli_result_t *res);

typedef enum {
    FMT_TEXT,       /* ordinary output                                    */
    FMT_SHUTDOWN    /* the server announced that it is shutting down      */
} cli_fmt_t;

/* Convert one server line to display text in `out`. */
cli_fmt_t cli_format(const char *server_line, char *out, size_t outsz);

/* Help text shown by the `help` command. */
const char *cli_help_text(void);

/* Is `name` a legal username ([A-Za-z0-9_.-], 1..MAX_USERNAME)? */
int cli_name_ok(const char *name);

#endif
