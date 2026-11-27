# Jofrey's part: Server core & concurrency — execution plan

Scope (from the work distribution): `server.c`, `net.c`, `session.c`, plus the
minimum glue needed to run and test the server end to end
(`client_handler.c`, `commands.c`, `users.c`, `db_stub.c`, `log.c`).

Ibrahim replaces `db_stub.c` (implements `db.h`) and extends `commands.c`/`users.c`.
Krishang writes the real client against `docs/PROTOCOL.md`.

## Tasks (each: write failing test → implement → pass → commit)

1. **Protocol + net layer** — `protocol.h`, `net.c`: listen socket, write-all, line reader.
   Test: `tests/test_net.c` (partial reads, CRLF, over-long lines, EOF).
2. **Session table** — `session.c`: slots, login with duplicate rejection, targeted send,
   list, broadcast, shutdown-all, wait-empty. All under mutexes.
   Test: `tests/test_session.c` (dup race with 32 threads, capacity, send, list).
3. **Command dispatch + storage interface** — `commands.c`, `db.h`, `db_stub.c`, `users.c`.
   Test: `tests/test_commands.c`.
4. **Client thread + server main** — `client_handler.c`, `server.c`: poll accept loop,
   thread per client, self-pipe signal handling, shutdown broadcast.
5. **Integration tests** — `tests/integration.py`: 5+ concurrent clients, duplicate login,
   invalid user, live + offline delivery, history/delete, 8-client stress ring,
   SIGINT broadcast, malformed input, server-full.
6. **Sanitizers** — run everything with `-fsanitize=thread` and `address,undefined`.
7. **Docs + publish** — README, PROTOCOL.md, DESIGN-server.md; push to GitHub (public).

## Global constraints
- C11, Linux, `-Wall -Wextra -Werror`, pthreads, no external libraries.
- Lock order: delivery lock → table lock → per-session write lock.
- Only the server stores messages; clients keep nothing.
