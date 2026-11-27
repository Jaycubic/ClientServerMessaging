# Server design (core & concurrency)

## Architecture

```
                         +---------------------------------------------+
                         |                  server process             |
  Ctrl+C / SIGTERM ----> | signal handler --writes byte--> wake pipe   |
  console "shutdown" --> |                                    |        |
                         |  main thread: poll(listen fd, wake pipe,    |
                         |               stdin if tty)                 |
                         |     | accept()                              |
                         |     v                                       |
                         |  session_open() --> slot in session table   |
                         |     | pthread_create (detached)             |
                         |     v                                       |
  client A <--TCP--> [ client_thread A ] --+                            |
  client B <--TCP--> [ client_thread B ] --+--> commands.c --> db.h     |
  client C <--TCP--> [ client_thread C ] --+       |                    |
                         |                         v                    |
                         |            session table (mutex-protected)   |
                         +---------------------------------------------+
```

* **Process management** – one server process, one process per client (separate programs).
* **Concurrency** – thread-per-connection. The main thread only accepts; each
  `client_thread` owns its socket, reads lines with a private buffer and runs
  commands. A blocked `recv()` costs one thread, not the whole server.
* **I/O multiplexing** – the main thread `poll()`s the listening socket, the
  signal self-pipe and (when interactive) stdin in one call.
* **IPC** – TCP sockets. Clients never talk to each other; the server routes.

## Modules

| File | Responsibility |
|---|---|
| `server.c` | argument parsing, listen/accept loop, signals, graceful shutdown |
| `net.c` | listen socket, `net_write_all` (EINTR/short-write safe, `MSG_NOSIGNAL`), buffered `line_reader_t` |
| `session.c` | connection table, login/duplicate check, targeted send, broadcast, list |
| `client_handler.c` | per-connection thread: login handshake then command loop |
| `commands.c` | command parsing and execution |
| `users.c` | valid-user file + username syntax check |
| `db.h` / `db_stub.c` | storage interface and in-memory placeholder |
| `log.c` | mutex-protected timestamped event log |

## Shared data and synchronisation

| Shared data | Protected by | Notes |
|---|---|---|
| session table (`state`, `name`, `fd`) | `table_lock` | one mutex; critical sections are short and never block on I/O except bounded sends |
| bytes written to one socket | per-session `wlock` | a reply and a pushed `MSG` can come from different threads; lines never interleave |
| "is user online" + "store message" vs "login" + "fetch unread" | `delivery_lock` | closes the race where a message arrives between a login's unread-fetch and the sender's online check, which would strand it until the next login |
| message storage | internal mutex in `db_stub.c` | every `db_*` call is thread-safe |
| stdout log | `log_lock` | |
| valid-user list | none | loaded once before threads start, read-only afterwards |

**Lock order:** `delivery_lock` → `table_lock` → session `wlock`. Never acquired
in any other order, so no deadlock. A thread only ever writes to its own socket
(`wlock` only) or to another user's under `table_lock`.

**Duplicate sessions:** `session_login()` checks and claims the name inside one
`table_lock` critical section, so two simultaneous logins of the same user
cannot both succeed (tested with 32 racing threads × 20 rounds).

**Use-after-close:** `session_close()` marks the slot free and closes the fd
while holding `table_lock`; other threads only write to a slot after finding it
`LOGGED_IN` under that same lock, so they can never write to a closed or
reused fd.

**Slow client protection:** accepted sockets get `SO_SNDTIMEO` (3 s) so a client
that stops reading cannot stall the threads that push messages to it.

## Shutdown

1. `SIGINT`/`SIGTERM` handler writes one byte to the wake pipe (the only
   async-signal-safe work done); the console command `shutdown` does the same
   thing from the main loop.
2. Main loop leaves `poll()`, closes the listening socket (no new clients).
3. `session_broadcast("SHUTDOWN|Server shutting down...")` to every logged-in client.
4. `session_shutdown_all()` calls `shutdown(fd, SHUT_RDWR)` on every socket so
   threads blocked in `recv()` wake with EOF and clean up (`session_close`).
5. `session_wait_empty(5)` waits on a condition variable until the table is
   empty, then the database is closed and the process exits 0.

## Testing

* `tests/test_net.c` – line reader (partial reads, CRLF, over-long, EOF), writer, SIGPIPE safety.
* `tests/test_session.c` – login/duplicate, re-login, capacity, broadcast, duplicate-login race, 8 threads × 200 concurrent writes with no interleaving.
* `tests/test_commands.c` – every command incl. error paths.
* `tests/integration.py` – real server binary over TCP: 8 concurrent clients, duplicate login, invalid user, live and offline delivery, history/delete, 8-client × 60-message ring stress, login-vs-send race, malformed input, server-full, SIGINT/SIGTERM broadcast, shutdown under load.
* `make tsan` / `make asan` rebuild and run everything under ThreadSanitizer / AddressSanitizer+UBSan.

## Known limitations / hand-offs

* `db_stub.c` is in-memory (messages are lost on restart). Ibrahim's persistent
  implementation must satisfy `db.h` and be thread-safe.
* A live `MSG` that is written to a socket whose peer has silently died (no FIN
  yet) is recorded as delivered.
* Usernames are case-sensitive.
