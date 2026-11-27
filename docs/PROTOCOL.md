# Wire protocol

Transport: TCP. One message = one line terminated by `\n` (a trailing `\r` is
tolerated). Fields are separated by `|`. The **last** field may contain `|`.
Max line length 2048 bytes; max chat text 1024 bytes. Usernames match
`[A-Za-z0-9_.-]{1,32}` and must be listed in `users.txt`.

Constants live in `src/protocol.h`.

## Client → server

| Line | Meaning | CLI command the client maps it from |
|---|---|---|
| `CONNECT\|<user>` | Log in. Must be the first line sent. | `connect <server IP> <user>` |
| `SEND\|<to>\|<text>` | Send a message | `sendmessage <user> <message>` |
| `GETMSGS\|<user>` | History with `<user>` | `getmessages <user>` |
| `DELMSGS\|<user>` | Delete history with `<user>` | `deletemessages <user>` |
| `USERS` | List online users | `getuserlist` |
| `QUIT` | Disconnect | `shutdown` |

Command names are case-insensitive.

## Server → client

| Line | When |
|---|---|
| `OK\|<info>` | Command succeeded. Login: `OK\|Welcome <user> (<n> unread)` |
| `ERR\|<reason>` | Command failed (e.g. `ERR\|Invalid user 'x'`, `ERR\|User already connected`, `ERR\|Server full`) |
| `UNREAD\|<from>\|<unix-ts>\|<text>` | Sent right after the login `OK`, one per offline message (`<n>` of them) |
| `MSG\|<from>\|<text>` | Real-time message pushed to an online user. Display as `Message from <from>: <text>` |
| `HIST\|<from>\|<to>\|<unix-ts>\|<text>` | One per stored message in reply to `GETMSGS`, oldest first, followed by `OK\|<n> message(s) with <user>` |
| `USERLIST\|<u1>,<u2>,...` | Reply to `USERS` (empty list = `USERLIST\|`) |
| `SHUTDOWN\|Server shutting down...` | Broadcast when the server stops. The server then closes the connection. |

## Session rules

1. The first line must be `CONNECT|<user>`. Any failure (bad format, unknown
   user, duplicate session) gets an `ERR|...` line and the connection is closed.
2. After login, every command line gets exactly one `OK|`/`ERR|`/`USERLIST|`
   reply (`GETMSGS` sends `HIST|` lines first). `MSG|` pushes can arrive at any
   time between replies, so the client needs a dedicated receive thread.
3. A line over 2048 bytes gets `ERR|Line too long` and the connection closes.
4. EOF from the server means the server went away; the client should exit.

## Example

```
C: CONNECT|alice            S: OK|Welcome alice (1 unread)
                            S: UNREAD|carol|1763404501|call me
C: SEND|bob|hi there        S: OK|Delivered to bob          (bob sees MSG|alice|hi there)
C: SEND|dave|ping           S: OK|dave is offline, message stored
C: SEND|zed|x               S: ERR|Invalid user 'zed'
C: USERS                    S: USERLIST|alice,bob
C: QUIT                     S: OK|Goodbye
```
