# Complete Documentation: Multi-User Client-Server Messaging Application

> **Who this is for.** Anyone in the group (or the professor) who needs to *use* the system, *understand every file and function*, know *why each decision was made*, and be ready for the viva where code may have to be explained or modified on the spot. Read Part 1 to run it, Parts 2-4 to understand the code, Part 5 for the reasoning, Part 6 to prepare for questions.

# Part 1. Using the system

## 1.1 What it is

A chat system for a handful of users on a network. A **server** program keeps running; each user starts a **client** program, logs in with a user name, and can send messages to other users, receive messages instantly, read past conversations, delete them, and see who is online. If a user is offline, messages wait on the server and appear when they next log in.

## 1.2 Build and start

You need gcc, make and python3 (python only for the tests). On the shared `CSIT203` account everything is already installed and the project is in `~/ClientServerMessaging`.

```bash
cd ~/ClientServerMessaging
make                          # builds build/server and build/client
./build/server -p 5555        # terminal 1: start the server
./build/client                # terminal 2, 3, ...: start clients
```

Server options: `-p <port>` (default 5555; 0 = pick a free port), `-u <file>` valid users (default `users.txt`), `-d <file>` message store (default `messages.dat`). Stop the server with **Ctrl+C**, or type `shutdown` in its terminal (`users` lists who is online).

Client: `./build/client [-p port]`, or `./build/client <server-ip> <user>` to connect immediately.

> **Firewalls.** If clients on other machines cannot connect, the port (5555) may be blocked by a firewall or network policy (the assignment brief mentions this). On the same machine use `127.0.0.1`.

## 1.3 Client commands

| Command | What it does |
|---|---|
| `connect <server IP> <user name> [port]` | Log in. The name must be in `users.txt`. |
| `sendmessage <user> <message>` | Send a message (the text may contain spaces and `\|`). |
| `getmessages <user>` | Show all earlier messages exchanged with that user. |
| `deletemessages <user>` | Delete all messages to/from that user from the server. |
| `getuserlist` | Show users currently online. |
| `shutdown` | Disconnect and exit the client. |
| `help` | Show the command list. |

## 1.4 Example session

Terminal 1 (server) prints events as they happen:

```
[10:02:11] Listening on port 5555 (13 valid users). Ctrl+C or 'shutdown' to stop.
[10:02:20] user alice connected from 127.0.0.1:51514
[10:02:31] user bob connected from 127.0.0.1:51520
[10:02:40] alice -> bob: message (delivered)
```

Alice's terminal:

```
> connect 127.0.0.1 alice
Welcome alice (0 unread)
> getuserlist
Online users (2): alice, bob
> sendmessage bob hello bob
Delivered to bob
> sendmessage carol are you there?
carol is offline, message stored
```

Bob's terminal (the message appears immediately, even while he is typing):

```
Message from alice: hello bob
```

Later, Carol logs in and sees what she missed:

```
> connect 127.0.0.1 carol
Welcome carol (1 unread)
[unread 2025-11-17 10:03:02] Message from alice: are you there?
> getmessages alice
[2025-11-17 10:03:02] alice -> carol: are you there?
1 message(s) with alice
```

## 1.5 Error messages you may see

| Message | Meaning |
|---|---|
| `Error: Invalid username` | Not in `users.txt` or illegal characters. The connection is closed; you can retry `connect`. |
| `Error: User already connected` | That user is logged in elsewhere. Only one session per user is allowed. |
| `Error: Server full` | 64 connections already in use. |
| `Error: Invalid user 'x'` | The target of `sendmessage`/`getmessages`/`deletemessages` is not a valid user. |
| `Error: Not connected. Use: connect <server IP> <user name>` | Log in first. |
| `Error: cannot connect to <ip>:<port>` | Server not running, wrong address/port, or blocked by a firewall. |
| `Disconnected: connection to server lost.` | The server died without saying goodbye (client exits with code 1). |

## 1.6 Running the tests

```bash
make test       # 326 unit checks + 47 end-to-end tests
make tsan       # same, under ThreadSanitizer (data-race detector)
make asan       # same, under AddressSanitizer + UBSan (memory errors)
```

## 1.7 The shared CSIT203 account

Created by `scripts/setup_csit203_user.sh` (run once by the administrator with sudo). The account has **no sudo rights and cannot install software**; it has a private home directory, the toolchain is installed system-wide, and the project is cloned, built and tested in its home. Team members connect with `ssh CSIT203@<server>` and read `~/README_FIRST.txt`. Please keep experiments inside the home directory and use different ports (`-p`) if someone else's server is already running on 5555.

<!-- pagebreak -->

# Part 2. Repository tour

| Path | What it is |
|---|---|
| `src/protocol.h` | Constants (`MAX_LINE`, `MAX_TEXT`, `MAX_USERNAME`, `MAX_CLIENTS`, `DEFAULT_PORT`) and the names of all message kinds. Shared by server and client. |
| `src/net.c`, `net.h` | Networking helpers: `net_listen`, `net_write_all`, `net_write_line`, `net_set_send_timeout` and the buffered `line_reader_t` (`lr_init`, `lr_read`). Used by server **and** client. |
| `src/session.c`, `session.h` | The table of connections and logged-in users and all locking around it. |
| `src/client_handler.c`, `.h` | The function each server thread runs: login, then command loop. |
| `src/commands.c`, `.h` | What each protocol command does on the server. |
| `src/db.h` | The storage interface (what the server needs from a database). |
| `src/db_file.c` | The persistent implementation of `db.h` (append-only log file). |
| `src/users.c`, `.h` | Loads `users.txt`; checks names. |
| `src/log.c`, `.h` | `log_event()`: timestamped, thread-safe console output. |
| `src/server.c` | `main()` of the server. |
| `client/client.c` | `main()` of the client, two threads. |
| `client/cli.c`, `.h` | Pure functions: command parser and output formatter. |
| `tests/test_*.c` | C unit tests; `tests/test_util.h` is a tiny assertion helper. |
| `tests/integration.py` | Starts the real server, talks raw protocol. |
| `tests/e2e_client.py` | Starts the real server **and real clients**. |
| `users.txt` | List of valid users. |
| `Makefile` | Build, test, sanitizer targets. |
| `docs/` | This document, the Design Document, `PROTOCOL.md`, diagrams. |
| `scripts/` | Account setup script, diagram and document generators. |

# Part 3. How the pieces work together: following one message

The best way to understand the code is to follow what happens when alice types `sendmessage bob hi`.

1. **Client main thread**: `client.c: main()` is blocked in `poll()` on stdin. Alice presses Enter. It `read()`s the bytes into `inbuf`, finds the `\n`, and calls `handle_input("sendmessage bob hi")`.
2. `handle_input` calls **`cli_parse()`** (`cli.c`). It splits off the command word `sendmessage`, then the user token `bob`, trims the rest and builds the protocol line **`SEND|bob|hi`**, returning kind `CLI_SEND`.
3. `handle_input` calls **`net_write_line()`** (`net.c`) which appends `\n` and sends the bytes with `send(..., MSG_NOSIGNAL)`.
4. **Server client-thread for alice** (created earlier in `server.c` by `pthread_create(client_thread)`) is blocked inside **`lr_read()`** in `client_handler.c`. Data arrives, `lr_read` finds a complete line and returns it.
5. Alice is already logged in, so `client_thread` calls **`cmd_handle(s, "SEND|bob|hi")`** (`commands.c`). It splits at the first `|` into command `SEND` and args `bob|hi` and calls **`do_send()`**.
6. `do_send()` splits args into `to="bob"`, `text="hi"` and validates: `check_user_arg` (well-formed and in `users.txt`), not alice herself, text not empty, text ≤ 1024.
7. It takes **`session_delivery_lock()`**, builds the push line `MSG|alice|hi`, and calls **`session_send_user("bob", ...)`** (`session.c`). That takes `table_lock`, finds bob's slot (`SLOT_LOGGED_IN` with matching name), takes bob's `wlock`, writes the line to bob's socket and unlocks. Returns 0 if bob is online and the write worked, otherwise -1.
8. `do_send()` calls **`db_save(alice, bob, "hi", delivered)`** (`db_file.c`): under the db mutex it fills a `rec_t`, appends a line to `messages.dat`, `fsync`s, and only then counts the record. Then it releases the delivery lock.
9. `do_send()` replies to alice via **`reply()` → `session_send()`**: `OK|Delivered to bob` (or `OK|bob is offline, message stored`) and logs `alice -> bob: message (delivered)` on the server console.
10. **Bob's client receiver thread** is blocked in `lr_read()`. It gets `MSG|alice|hi`, calls **`cli_format()`** which produces `Message from alice: hi`, and **`ui_print()`** prints it (clearing and redrawing the prompt on a terminal).
11. **Alice's client receiver thread** receives `OK|Delivered to bob`, formats it as `Delivered to bob`, prints it.

Offline case: in step 7 bob is not in the table so `session_send_user` returns -1 and the record is saved with `delivered = 0`. When bob later logs in, `do_login()` calls `db_take_unread("bob")`, which returns the undelivered records and marks them delivered (appending a `V` line to the file), and the server sends them as `UNREAD|alice|<time>|hi`.

<!-- pagebreak -->

# Part 4. Function-by-function reference

Notation: **Purpose** (what), **How** (the mechanism), **Why** (the reasoning), **Edge cases**.

## 4.1 `protocol.h`

Only `#define`s. `MAX_USERNAME 32`, `MAX_LINE 2048` (longest line), `MAX_TEXT 1024` (longest chat text), `MAX_CLIENTS 64` (session slots), `DEFAULT_PORT 5555`; command names (`CMD_SEND` …), response names (`RSP_MSG` …) and `SHUTDOWN_TEXT`. Keeping them in one header guarantees client and server agree. *To change a limit, change it here and rebuild both programs.*

## 4.2 `net.c` (networking)

**`int net_listen(int port)`**
- *Purpose:* create the server's listening TCP socket.
- *How:* `socket(AF_INET, SOCK_STREAM)`, `setsockopt(SO_REUSEADDR)`, `bind` to `INADDR_ANY:port`, `listen(fd, 64)`. Returns the fd or -1 (errno preserved across the cleanup `close`).
- *Why `SO_REUSEADDR`:* after the server stops, the port stays in TIME_WAIT; without this option restarting the server immediately fails with "Address already in use".
- *Edge:* `port = 0` lets the kernel choose a free port (used by tests; `server.c` prints the chosen one).

**`int net_write_all(int fd, const char *buf, size_t n)`**
- *Purpose:* write exactly `n` bytes.
- *How:* loop on `send(fd, buf+off, n-off, MSG_NOSIGNAL)`; retry on `EINTR`; add the partial count each time; -1 on any other error.
- *Why:* `send()` may write fewer bytes than asked (short write); correct code must loop. `MSG_NOSIGNAL` turns "peer closed" into an error return instead of killing the process with `SIGPIPE`.

**`int net_write_line(int fd, const char *line)`**
- *Purpose:* send one protocol line.
- *How:* copies up to `MAX_LINE` bytes into a local buffer, replacing any `\n`/`\r` inside with a space, appends `\n`, calls `net_write_all`. One `send` per line keeps a line contiguous.
- *Why replace newlines:* a stray newline inside a payload would split one message into two and desynchronise the protocol.

**`int net_set_send_timeout(int fd, int seconds)`** sets `SO_SNDTIMEO`. A `send()` to a client that stopped reading would otherwise block forever; here it fails after `seconds`, so one stalled client cannot freeze the threads that push messages to it.

**`line_reader_t`, `lr_init`, `lr_read`**
- *Purpose:* turn TCP's byte stream into lines.
- *State:* `fd`, a buffer of `MAX_LINE + 2` bytes, `len` = bytes currently buffered. One reader per connection, used by one thread only.
- *`lr_read` algorithm:* loop { look for `\n` in the buffer with `memchr`; if found, copy the line out (dropping a trailing `\r`), `memmove` the remaining bytes to the front, return `LR_LINE`; else if the buffer already holds more than `MAX_LINE` bytes without a newline return `LR_TOO_LONG`; else `recv()` more (retry on `EINTR`; return `LR_ERROR` on error, `LR_EOF` on 0 bytes) }.
- *Why:* TCP does not preserve message boundaries: one `recv` may return half a line, or three lines at once. The buffer reassembles lines correctly in both cases (tested with byte-by-byte writes).
- *Edge:* an over-long line returns `LR_TOO_LONG` and the caller closes the connection, which prevents a client from making the server buffer unbounded data.

## 4.3 `session.c` (connection table and locking)

State: `table[MAX_CLIENTS]`, `table_lock`, `table_empty` (condition variable), `delivery_lock`, `used_slots`.

**`void session_table_init(void)`** zeroes every slot, sets `fd = -1`, initialises each slot's `wlock`. Called once at start (and by unit tests to reset).

**`session_t *session_open(int fd, const char *addr)`** finds the first `SLOT_FREE` slot under `table_lock`, marks it `SLOT_CONNECTED`, stores `fd`/address, increments `used_slots`, returns it, or `NULL` if all 64 are used (server full). *Why claim at accept time:* shutdown must be able to reach connections that never logged in.

**`int session_login(session_t *s, const char *name)`** under `table_lock` scans all slots; if another slot is `SLOT_LOGGED_IN` with the same name returns -1, otherwise copies the name into `s` and sets `SLOT_LOGGED_IN`, returns 0. *Why one critical section:* check and claim must be indivisible or two threads could both pass the check (the classic check-then-act race).

**`void session_close(session_t *s)`** under `table_lock`: if not already free, mark `SLOT_FREE`, clear the name, `close(fd)`, `fd = -1`, `used_slots--`, and `pthread_cond_broadcast(&table_empty)` when it reaches 0. *Why close the fd inside the lock:* other threads write to a slot only after finding it `LOGGED_IN` under the same lock, so they can never write to a closed or recycled descriptor.

**`int session_send(session_t *s, const char *line)`** locks the session's own `wlock`, writes the line, unlocks. Used by a thread replying to *its own* client.

**`int session_send_user(const char *name, const char *line)`** locks `table_lock`, finds the logged-in slot named `name`, locks that slot's `wlock`, writes, unlocks both. Returns 0 on success, -1 if the user is not online or the write failed. Lock order table → wlock.

**`int session_list(char *out, size_t outsz)`** builds `alice,bob,carol` of all logged-in names into `out` (stops cleanly if the buffer is too small), returns the count.

**`int session_is_online(const char *name)`**, **`int session_count(void)`** simple locked queries.

**`int session_broadcast(const char *line)`** writes a line to every logged-in session (each under its `wlock`); returns how many were reached. Used for the shutdown notice.

**`void session_shutdown_all(void)`** calls `shutdown(fd, SHUT_RDWR)` on every occupied slot. *Why:* a thread blocked in `recv()` returns 0 (EOF) as soon as its socket is shut down, so every client thread wakes and cleans up by itself.

**`int session_wait_empty(int seconds)`** waits on `table_empty` with `pthread_cond_timedwait` until `used_slots == 0` or the deadline; returns 1 if empty. Standard condition-variable pattern: lock, `while (condition not met) wait`, unlock.

**`session_delivery_lock()/unlock()`** lock/unlock `delivery_lock`. See Part 5.4.

## 4.4 `client_handler.c` (the server thread for one client)

**`static int do_login(session_t *s, const char *line)`**
1. Parse `CONNECT|<name>`; wrong format → `ERR|Expected CONNECT|<user>` / `ERR|Not connected…`, return -1.
2. `users_name_wellformed(name) && users_is_valid(name)` else `ERR|Invalid username`.
3. `session_delivery_lock()`; `session_login()`; if duplicate, unlock and `ERR|User already connected`.
4. `db_take_unread(name)` to fetch offline messages, send `OK|Welcome <name> (<n> unread)` then `n` × `UNREAD|from|ts|text`, unlock, free, log "user X connected from addr".
Any failure returns -1 and the caller closes the connection. *Why hold the delivery lock until all unread lines are written:* so no live `MSG` can overtake the stored messages, keeping chronological order.

**`void *client_thread(void *arg)`**: the thread entry. Creates a `line_reader_t`, then loops `lr_read`: `LR_TOO_LONG` → `ERR|Line too long` and stop; EOF/error → stop; if not logged in → `do_login` (failure stops, success sets `logged_in`); else `cmd_handle` (returns `CMD_CLOSE` for QUIT → stop). After the loop: log the disconnect if logged in, `session_close(s)`, return. The thread is *detached*, so nobody has to `join` it; the table's "empty" condition tells the main thread when all have finished.

## 4.5 `commands.c` (server-side commands)

**`static void reply(session_t *s, const char *kind, const char *fmt, ...)`** printf-style helper: formats the body, prefixes `kind|`, sends with `session_send`.

**`static int check_user_arg(session_t *s, const char *user)`** returns 1 if `user` is well-formed and in the valid-user list; otherwise replies `ERR|Invalid user '<user>'` and returns 0. Shared by SEND, GETMSGS, DELMSGS so all give the same error.

**`static void do_send(session_t *s, char *args)`** steps: find the second `|` (missing → `ERR|Usage…`); validate recipient, not self, non-empty, ≤ `MAX_TEXT`; build `MSG|<me>|<text>`; **under `delivery_lock`** try `session_send_user` then `db_save(…, delivered)`; reply `OK|Delivered to X`, `OK|X is offline, message stored` or `ERR|Storage failure…`; log it.

**`static void do_getmessages(session_t *s, const char *user)`** missing arg → usage error; invalid user → error; otherwise `db_history(me, user)` and for each message send `HIST|from|to|ts|text`, then `OK|<n> message(s) with <user>`; frees the array.

**`static void do_deletemessages(session_t *s, const char *user)`** same validation; `db_delete(me, user)` removes messages in both directions; replies `OK|Deleted <n> message(s) with <user>`.

**`static void do_users(session_t *s)`** `session_list` → `USERLIST|a,b,c`.

**`int cmd_handle(session_t *s, const char *line)`** copies the line, splits at the first `|` into command + args, dispatches with `strcasecmp`; `QUIT` replies `OK|Goodbye` and returns `CMD_CLOSE`; `CONNECT` after login → `ERR|Already connected`; anything else → `ERR|Unknown command`. Always sends exactly one reply (or the HIST…OK group).

## 4.6 `users.c`

**`int users_name_wellformed(const char *name)`** true if length 1..32 and every char is a letter, digit, `_`, `-` or `.`. *Why:* the allowed set contains no `|`, `/`, spaces or control characters, so a name can never break the protocol, the file format or a path.

**`int users_load(const char *path)`** reads the file line by line; trims whitespace; skips blank lines, `#` comments and malformed names; stores up to 256 names. Returns the count or -1 if the file cannot be opened. **`users_is_valid(name)`** linear search (tiny list). **`users_count()`**.

## 4.7 `log.c`

**`void log_event(const char *fmt, ...)`** formats a `[HH:MM:SS]` prefix with `localtime_r` (the re-entrant version, safe in threads), prints the message under `log_lock` and flushes. *Why the lock:* two threads printing at once would otherwise interleave characters within a line. **`log_set_quiet()`** silences output in unit tests.

## 4.8 `db_file.c` (storage implementation of `db.h`)

State: `recs[]` (array), `nrecs`, `caprecs`, `next_id`, `file_path`, `logf` (append handle), one `lock`.

**Helpers**
- `escape()` / `unescape()`: convert text ↔ one-line form (`\`→`\\`, LF→`\n`, CR→`\r`, TAB→`\t`).
- `write_rec(FILE*, rec)`: prints one `M\t…` line.
- `sync_file(FILE*)`: `fflush` then `fsync(fileno)`: push data from the C library buffer to the kernel and from the kernel to the disk.
- `write_snapshot(path, recs, n)`: writes all records to `path.tmp` (mode 0600), `fsync`s, `fclose`s, then `rename(tmp, path)`, which atomically replaces the old file; cleans up the temp file on error.
- `append_line(fmt, id)`: appends a `V` line and syncs.
- `reserve(n)`: grows the array (doubling) with `realloc`.
- `reset_state()`: closes the file, frees memory, resets counters.
- `replay_line(line)`: parses one log line. `V` marks a record delivered; `M` is validated field by field (numeric id/timestamp, delivered flag 0/1, non-empty bounded user names) and appended to the array; returns -1 for malformed lines (the caller counts and skips them). Updates `next_id`.
- `load_file(path)`: reads with `getline`; a last line without `\n` is a *torn write* from a crash and is ignored; logs how many bad lines were skipped.
- `between(rec, a, b)`: true if the record is a→b or b→a.

**`int db_init(const char *path)`** resets state; `path == NULL` → memory-only; otherwise load the file (missing file is fine), **compact** it with `write_snapshot` (drops obsolete `V` lines and any garbage), and open it for appending.

**`void db_close(void)`** final sync and reset.

**`int db_save(from, to, text, delivered)`** validates lengths, fills a record with the next id and `time(NULL)`, appends and syncs the log line, and **only if that succeeded** adds it to the array and bumps the id. *Why:* "acknowledged ⇒ durable". If the disk write fails the caller gets -1 and tells the client.

**`int db_take_unread(user, &out, &n)`** returns (in a `malloc`ed array the caller frees with `db_free`) every undelivered message addressed to `user`, oldest first, marking each delivered in memory and appending a `V` line (if that append fails the only consequence is a re-delivery after a restart).

**`int db_history(a, b, &out, &n)`** all messages between two users, both directions, oldest first.

**`int db_delete(a, b)`** counts matches; builds the surviving array; `write_snapshot` of the survivors; reopens the append handle; swaps the arrays; returns the number deleted (0 if none, -1 on failure, in which case memory and file are both left unchanged).

**`void db_free(msgs)`** `free()` wrapper so callers do not need to know the allocation method.

## 4.9 `server.c`

- `on_signal(int sig)`: the signal handler; writes one byte to `wake_pipe[1]`, preserving `errno`. Nothing else is safe to do in a handler.
- `usage()`, `handle_console()`: prints usage; reads one console line: `shutdown`/`quit` → returns 1, `users` → logs the online list, EOF → returns -1 (ignore the console from now on).
- **`main()`:**
  1. Parse `-p/-u/-d`; load users (`users_load`); open store (`db_init`); `session_table_init`.
  2. Create the wake pipe; install `on_signal` for `SIGINT`/`SIGTERM` with `sigaction` (no `SA_RESTART`, so blocking calls return `EINTR`); ignore `SIGPIPE`.
  3. `net_listen`; if `port` was 0, `getsockname` to learn and print the real port.
  4. **Loop:** build the `pollfd` array (listen fd, wake pipe, stdin if it is a tty); `poll(-1)`; on `EINTR` continue; if the wake pipe is readable → shutdown; if stdin is readable → `handle_console`; if the listen fd is readable → `accept`, format `ip:port`, `net_set_send_timeout`, `session_open` (full → `ERR|Server full`, close), `pthread_create` a **detached** thread running `client_thread` (on failure: error reply and `session_close`).
  5. **Shutdown:** log; `close(listen fd)`; `session_broadcast("SHUTDOWN|Server shutting down...")`; `session_shutdown_all()`; `session_wait_empty(5)`; `db_close()`; log "Shutdown complete"; `return 0`.

## 4.10 `client/cli.c` (pure functions)

**`int cli_name_ok(const char *name)`** same character rules as the server (client-side check gives instant feedback and keeps `|` out of protocol lines).

**`static int next_token(const char **p, char *tok, size_t toksz)`** skips whitespace, copies one whitespace-delimited token, advances the pointer; returns 0 if there is none. Used for all argument splitting.

**`static void fail(res, msg)`** helper: sets `CLI_ERROR` and the message.

**`parse_connect`**: needs host and user (optional port 1..65535, validated with `strtol`); **`parse_sendmessage`**: user token, then *everything else* (leading/trailing whitespace trimmed, inner spacing preserved) as the text; rejects empty, bad name, text > 1024; builds `SEND|user|text`. **`parse_one_user`**: for `getmessages`/`deletemessages`: exactly one valid user token else usage error.

**`void cli_parse(const char *input, int connected, cli_result_t *res)`** zeroes `res`; reads the command word; `help`/`?`, `shutdown`, `connect` (refused if already connected); any other command when not connected → "Not connected…"; then `sendmessage`, `getmessages`, `deletemessages`, `getuserlist`; unknown → error. Case-insensitive.

**`static char *field(char **p)`** splits the next `|` field in place. **`static void fmt_time(ts, out, outsz)`** unix time → `YYYY-MM-DD HH:MM:SS` via `localtime_r`/`strftime`.

**`cli_fmt_t cli_format(const char *server_line, char *out, size_t outsz)`** `MSG` → `Message from <from>: <text>`; `UNREAD` → `[unread <time>] Message from …`; `HIST` → `[<time>] <from> -> <to>: <text>`; `USERLIST` → `Online users (n): a, b` or `No users online`; `ERR` → `Error: …`; `OK` → the text; `SHUTDOWN` → the text and returns `FMT_SHUTDOWN`; unknown → shown raw. Every branch tolerates missing fields (tested with malformed input) and never overruns `out`.

**`const char *cli_help_text(void)`** the help string.

## 4.11 `client/client.c`

Globals: `sock_fd`, `reader` (the line reader), `recv_thread`, `wake_pipe` (receiver → main), `sig_pipe` (signals → main), `ui_lock` (output mutex), atomics `quitting`, `got_shutdown_notice`, `lost_connection`.

- `ui_print(text)`: under `ui_lock`: if on a terminal, `\r\033[K` clears the current line; print text + newline; redraw `> `; flush. `ui_prompt()`: just the prompt.
- `wake_main()`: write a byte to `wake_pipe`. 
- **`receiver(void *)`** *(thread)*: `while (lr_read == LR_LINE) { cli_format; remember if it was a SHUTDOWN notice; ui_print }`. When the loop ends: if neither we initiated the quit nor the server announced shutdown, set `lost_connection`; then `wake_main()`.
- `drop_connection()`: `shutdown(fd, SHUT_RDWR)` (wakes the receiver), `pthread_join`, `close`.
- `tcp_connect(host, port)`: `getaddrinfo` (IPv4/IPv6, hostnames or IPs), tries each address; `SO_SNDTIMEO` bounds `connect()`.
- **`do_connect(cli_result_t *c)`**: connect; set a 5 s receive timeout; send `CONNECT|user`; read the reply line; `ERR` → print and close (client stays usable); `OK|Welcome … (n unread)` → print, parse `n` with `sscanf`, read and print exactly `n` UNREAD lines; clear the timeout; reset the flags; **start the receiver thread**.
- `on_signal`, `drain`: signal → pipe byte; read and discard pipe bytes.
- `graceful_disconnect()`: set `quitting`, send `QUIT`, `poll` the wake pipe up to 3 s for the receiver to finish (server closes after `OK|Goodbye`), then `drop_connection`.
- **`handle_input(const char *input)`**: `cli_parse`, then act on the kind: help, error, connect, send (`net_write_line`), shutdown (graceful disconnect, tell main to exit).
- **`main()`**: options; signals; optional `<ip> <user>` auto-connect; loop `poll({wake_pipe, sig_pipe, stdin})`: signal → graceful disconnect and exit; wake pipe → the receiver ended: print "server has shut down" (exit 0) or "connection lost" (exit 1); stdin → read, split into lines (own buffer, because `poll` would not report data already buffered by stdio), `handle_input` each; EOF on stdin → graceful disconnect.

<!-- pagebreak -->

# Part 5. Decision log: why it is built this way

## 5.1 Architecture decisions

| Decision | Alternatives considered | Why this choice |
|---|---|---|
| **Thread per client** | `fork` per client; one `poll` loop for all | Shared session table and store are natural with threads; per-client code is straight-line; blocking on one client never delays others; scale (5-64) is small. `fork` would need shared memory/IPC; a single loop needs state machines for partial I/O. |
| **`poll()` in the main thread** | blocking `accept()` + `pthread_kill`; `select` | Must wait on accept, signals and console at once. `poll` has no 1024-descriptor limit and no fd-set rebuilding quirks of `select`. |
| **Self-pipe for signals** | doing work in the handler; `sigwait` thread; `signalfd` | Only async-signal-safe calls are allowed in handlers; a pipe write is one and integrates with `poll`. `signalfd` is Linux-only; `sigwait` needs more thread/mask plumbing. |
| **Text line protocol** | binary structs; length-prefixed frames | Debuggable with `nc`; no endianness/packing issues; a newline is a trivial frame boundary. |
| **`\|` separator, last field free-form** | JSON; quoting/escaping | Simple parsing with split limits; chat text may contain `\|` without escaping. Names cannot contain `\|` (validated). |
| **Fixed 64-slot table** | linked list / hash map | No allocation, no leaks, trivial lookups at this size, natural capacity limit. |
| **Slot at accept time** | slot after login | Shutdown can reach not-yet-logged-in connections; a half-open connection still counts against capacity. |
| **Drop the connection on any login failure** | let the client retry on the same connection | Smaller state machine, nothing for an unauthenticated peer to probe. Our client lets the user retry `connect` (it reconnects). |
| **Flat-file storage behind `db.h`** | SQLite | No dependency (the shared account cannot install packages), shows `fsync`/`rename`/recovery explicitly, easy to explain; interface allows swapping. |
| **Append-only log + atomic rewrite on delete** | in-place edit; one file per user pair | Append is fast and crash-safe; `rename` is atomic; in-place edits can leave half-written files. |
| **Acknowledge after `fsync`** | acknowledge immediately | A message the user was told is "stored" must survive a crash. Cost: a disk sync per message, acceptable here. |
| **Memory-only mode (`db_init(NULL)`)** | mock objects | Lets unit tests run without touching the disk while using the real code. |
| **Client: parser/formatter as pure functions** | parse inline in `main` | Fully unit-testable with no sockets or terminal. |
| **Client handshake before the receiver thread** | let the receiver handle login replies | No race between "login finished" and "start printing"; simpler receiver. |

## 5.2 Robustness decisions

| Decision | Reason |
|---|---|
| `MSG_NOSIGNAL` and ignoring `SIGPIPE` | Writing to a dead client must be an error, not process death. |
| `SO_SNDTIMEO` 3 s | A stalled reader must not block threads that push to it while holding the table lock. |
| Maximum line length 2048 / text 1024 | Bounds memory per connection; prevents unbounded buffering by a hostile client. |
| Username character whitelist | Keeps `\|`, `/`, spaces, control characters out of the protocol, log and files. |
| Torn-line tolerance in the data file | A crash mid-append must not make the whole store unreadable. |
| `0600` file mode | Messages are private; no other local user may read the store. |
| Bounded waits (5 s shutdown wait, 3 s client quit wait, 5 s handshake) | A misbehaving peer can delay but never hang shutdown. |

## 5.3 Why `getmessages` shows both directions and `deletemessages` removes both

The brief says "all previous messages exchanged with `<user>`" and "all messages to/from `<user>`". Both are symmetric conversation operations, so the store matches a record if it is A→B or B→A. Deleting is therefore visible to both participants, because the server is the only copy.

## 5.4 The delivery lock, in detail

Without it, this interleaving loses a message:

```
Thread A (alice sends to bob)         Thread B (bob logs in)
1. is bob online?  -> no
                                       2. session_login(bob)
                                       3. db_take_unread(bob)  -> (nothing yet)
4. db_save(alice, bob, delivered=0)
   -> stored as unread, but bob already fetched his list: stranded until next login
```

`delivery_lock` makes steps 1+4 and steps 2+3 mutually exclusive, so either the sender sees bob online (live delivery) or the message is stored *before* bob fetches (delivered as UNREAD). The test `test_message_during_login_never_lost` hammers exactly this window.

## 5.5 Lock order, in detail

`delivery_lock → table_lock → wlock`, never the reverse. If thread X held `table_lock` and wanted `delivery_lock` while thread Y held `delivery_lock` and wanted `table_lock`, they would deadlock. A fixed global order makes that impossible. The db and log mutexes are *leaf* locks: no other lock is requested while holding them.

<!-- pagebreak -->

# Part 6. Preparing for the viva

## 6.1 Questions about the design

1. **Why threads and not processes for client handling?** Clients must share the user list and the message store; threads share memory, processes would need shared memory or IPC for every access. Threads are also cheaper to create.
2. **How many clients can the server handle?** 64 simultaneous connections (`MAX_CLIENTS`); the brief requires 5. The 65th gets `ERR|Server full`.
3. **How does the server notice Ctrl+C while blocked waiting for clients?** The main thread blocks in `poll()` on the listening socket *and* the wake pipe. The `SIGINT` handler writes a byte to the pipe, `poll()` returns.
4. **Why not do the shutdown work inside the signal handler?** Only async-signal-safe functions may be called there (`malloc`, `printf`, `pthread_mutex_lock` are not). The handler only writes one byte.
5. **How do you make blocked client threads exit at shutdown?** `shutdown(fd, SHUT_RDWR)` on each socket makes `recv()` return 0, so each thread falls out of its loop and calls `session_close`.
6. **How do you know all threads are finished?** `session_wait_empty()` waits on a condition variable that `session_close()` signals when the last slot is freed.
7. **What is the protocol and why text?** Lines with `|`-separated fields, see section 3 of the design document. Text is debuggable and needs no byte-order handling.
8. **What if a message arrives in two TCP segments, or two messages in one?** `lr_read` buffers and splits on `\n`, tested with byte-by-byte writes.
9. **How is a duplicate session prevented?** `session_login` checks all slots and claims the name inside one `table_lock` critical section.
10. **What happens to messages for offline users?** Saved with `delivered = 0`; fetched and marked delivered at next login by `db_take_unread`; displayed as UNREAD.
11. **What happens if the server crashes in the middle of saving?** Each save is appended and `fsync`ed before acknowledgement; a torn last line is ignored at start-up; earlier records are intact.
12. **How does delete stay safe against a crash?** Write a complete new file, `fsync`, then atomic `rename`.
13. **Where is each OS concept used?** Table in design document section 8.
14. **Why does the client need two threads?** One blocks on the keyboard, the other on the network; without two, incoming messages would only appear after the user pressed Enter.
15. **Why doesn't the client store anything?** Requirement; also avoids stale or divergent copies, the server is the single source of truth.
16. **What does `MSG_NOSIGNAL` do?** Makes `send()` return `EPIPE` instead of delivering `SIGPIPE` when the peer has closed.
17. **What is `SO_REUSEADDR` for?** Allows immediate restart despite TIME_WAIT sockets.
18. **Why is the thread detached?** Nobody needs its return value; detached threads free their resources on exit without a `pthread_join`.
19. **Why a condition variable for waiting, not a loop with `sleep`?** No polling delay, no wasted CPU; wakes exactly when the state changes.
20. **How did you test for race conditions?** 32-thread login race ×20, 8×200 concurrent write test, login-vs-send race test, the ring stress test, and ThreadSanitizer over the whole suite.

## 6.2 Questions about synchronisation

21. **Name every mutex and what it protects.** See design document 7.1.
22. **State the lock order and why it prevents deadlock.** `delivery_lock → table_lock → wlock`; a global total order means no cycle of waiting threads can exist.
23. **Why a per-session write lock in addition to the table lock?** A thread replying to its own client does not need the table lock; `wlock` prevents two threads' lines from interleaving on one socket.
24. **Could `session_send_user` block the whole server?** Only if a socket's send buffer is full; `SO_SNDTIMEO` (3 s) bounds that.
25. **What is a check-then-act race? Give an example here.** Checking "name not online" and then claiming it in two steps; fixed by doing both under one lock in `session_login`.
26. **Is it safe that the user list is read without a lock?** Yes: it is loaded before any thread exists and never modified afterwards.
27. **Why do the db functions take a mutex when callers already hold `delivery_lock`?** `delivery_lock` only covers send-vs-login; history/delete calls from other threads do not hold it, so the store needs its own lock.

## 6.3 "Modify the code on the spot" exercises

**A. Change the maximum number of clients to 10.** Edit `MAX_CLIENTS` in `src/protocol.h`, `make`. (The integration test `test_server_full` has `MAX_CLIENTS = 64` hard-coded; change it too.)

**B. Add a new command `whoami` that replies with your user name.**
1. `src/protocol.h`: add `#define CMD_WHOAMI "WHOAMI"`.
2. `src/commands.c`: add `static void do_whoami(session_t *s) { reply(s, RSP_OK, "You are %s", s->name); }` and in `cmd_handle` add `else if (strcasecmp(buf, CMD_WHOAMI) == 0) do_whoami(s);`.
3. `client/cli.c`: in `cli_parse` add `else if (strcasecmp(cmd, "whoami") == 0) { snprintf(res->line, sizeof res->line, "%s", CMD_WHOAMI); res->kind = CLI_SEND; }` (after the `connected` check) and mention it in `cli_help_text`.
4. Optionally add a check in `tests/test_commands.c` and rebuild with `make test`.

**C. Make the shutdown message different.** Change `SHUTDOWN_TEXT` in `protocol.h` and update the expected text in the tests.

**D. Limit message length to 200 characters.** Change `MAX_TEXT` in `protocol.h`; the server and client checks and the file code all use the constant.

**E. Log every command a user sends.** In `cmd_handle` add `log_event("%s: %s", s->name, line);` at the top.

**F. Print the number of online users when someone connects.** In `do_login` after the login: `char l[MAX_LINE]; int n = session_list(l, sizeof l); log_event("%d user(s) online", n);`.

**G. Change the port.** Run `./build/server -p 6000`; clients: `connect 127.0.0.1 alice 6000` or `./build/client -p 6000`. To change the default, edit `DEFAULT_PORT`.

**H. Make a user able to see who is online immediately after login.** In `do_login`, after the unread lines, call `session_list` and `session_send(s, "USERLIST|…")`.

## 6.4 Per-member checklist: be ready to explain

| Member | Explain these functions and ideas |
|---|---|
| **Jofrey** | `main()` of the server (poll loop, signal pipe, shutdown sequence), `net_listen`, `lr_read`, every function in `session.c`, `client_thread`/`do_login`, lock order, race conditions and how each is prevented. |
| **Ibrahim** | `db.h` contract, `db_file.c` (log format, `fsync`, atomic rename, replay/recovery, escaping), `users.c`, `commands.c` (`do_send` and the delivery-lock reasoning). |
| **Krishang** | Protocol (`PROTOCOL.md`), `cli.c` (`cli_parse`, `cli_format`), `client.c` (two threads, handshake, disconnect paths and exit codes), the test suites and what each test proves. |

Even so, be able to follow a message end to end across all modules (Part 3).

## 6.5 Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `bind: Address already in use` | Another server (maybe a teammate's on the shared account) uses the port: choose `-p <other>`. |
| Client: `cannot connect` | Server not running, wrong IP/port, firewall. Check with `nc -vz <ip> <port>`. |
| Messages from earlier runs appear | The store persists in `messages.dat`; delete the file (with the server stopped) for a clean slate or use `-d /tmp/x.dat`. |
| `cannot read user list` | Run the server from the project directory or pass `-u /path/users.txt`. |
| A user cannot log in | Name must be in `users.txt` (add it, restart the server). |
| Build error about warnings | The Makefile uses `-Werror`; fix the warning (it usually points to a real bug). |

# Part 7. Glossary

| Term | Meaning |
|---|---|
| **Socket** | An OS endpoint for network communication; here a TCP stream between a client and the server. |
| **Thread** | A unit of execution inside a process; threads share memory. |
| **Mutex** | Lock allowing one thread at a time into a critical section. |
| **Condition variable** | Lets a thread sleep until another thread signals that a condition changed. |
| **Critical section** | Code that touches shared data and must not run concurrently. |
| **Race condition** | A bug whose outcome depends on timing of threads. |
| **Deadlock** | Threads waiting for each other's locks forever. |
| **I/O multiplexing** | Waiting for several descriptors at once (`poll`). |
| **Async-signal-safe** | A function that may be called from a signal handler. |
| **Self-pipe trick** | Signal handler writes to a pipe so the main loop can wake via `poll`. |
| **`fsync`** | Forces file data from OS caches to the disk. |
| **Atomic rename** | `rename()` replaces a file in one indivisible step. |
| **Append-only log** | A file only ever written at the end; state is rebuilt by replaying it. |
| **TIME_WAIT** | State of a closed TCP connection that keeps the port reserved briefly. |
| **EOF** | End of file/stream; for a socket, the peer closed the connection. |
| **Detached thread** | A thread whose resources are freed automatically when it exits. |
