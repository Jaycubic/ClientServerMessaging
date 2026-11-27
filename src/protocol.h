/* protocol.h - shared constants and wire-format definitions.
 *
 * The wire protocol is line based: every message is one UTF-8/ASCII line
 * terminated by '\n' ('\r' is tolerated and stripped). Fields are separated by
 * '|'. The LAST field of a message may itself contain '|'.
 * See docs/PROTOCOL.md for the full specification.
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#define DEFAULT_PORT   5555
#define MAX_USERNAME   32      /* characters, excluding NUL */
#define MAX_LINE       2048    /* max bytes in one protocol line, excluding '\n' */
#define MAX_TEXT       1024    /* max bytes in one chat message body */
#define MAX_CLIENTS    64      /* connection slots (>= 5 required by the brief) */

/* Client -> server */
#define CMD_CONNECT    "CONNECT"   /* CONNECT|<user>              */
#define CMD_SEND       "SEND"      /* SEND|<to>|<text>            */
#define CMD_GETMSGS    "GETMSGS"   /* GETMSGS|<user>              */
#define CMD_DELMSGS    "DELMSGS"   /* DELMSGS|<user>              */
#define CMD_USERS      "USERS"     /* USERS                       */
#define CMD_QUIT       "QUIT"      /* QUIT                        */

/* Server -> client */
#define RSP_OK         "OK"        /* OK|<info>                   */
#define RSP_ERR        "ERR"       /* ERR|<reason>                */
#define RSP_MSG        "MSG"       /* MSG|<from>|<text>           (live)   */
#define RSP_UNREAD     "UNREAD"    /* UNREAD|<from>|<ts>|<text>   (login)  */
#define RSP_HIST       "HIST"      /* HIST|<from>|<to>|<ts>|<text>         */
#define RSP_USERLIST   "USERLIST"  /* USERLIST|<u1>,<u2>,...               */
#define RSP_SHUTDOWN   "SHUTDOWN"  /* SHUTDOWN|<text>                      */

#define SHUTDOWN_TEXT  "Server shutting down..."

#endif
