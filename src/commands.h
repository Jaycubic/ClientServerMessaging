/* commands.h - server-side handling of commands from a logged-in client. */
#ifndef COMMANDS_H
#define COMMANDS_H

#include "session.h"

#define CMD_CONTINUE 0
#define CMD_CLOSE    1   /* client asked to quit: close the connection */

/* Parse and execute one protocol line from logged-in session `s`.
 * Always writes a response (OK|... or ERR|...) to the client. */
int cmd_handle(session_t *s, const char *line);

#endif
