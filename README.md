# Multi-User Client-Server Messaging Application

OS group project (C, Linux): a multi-threaded messaging server and CLI client
demonstrating processes, threads, mutex synchronisation, socket IPC,
poll-based I/O multiplexing, persistent storage and signal handling.

| Member | Part |
|---|---|
| Jofrey | Server core & concurrency (`server.c`, `net.c`, `session.c`, `client_handler.c`) |
| Ibrahim | Storage & message logic (`db.h` implementation, `users.c`, `commands.c`) |
| Krishang | Client, protocol, tests & docs |

## Status

- [x] Server core: accept loop, thread per client, session table, duplicate-login rejection, signal handling, shutdown broadcast, console logging
- [x] Protocol spec ([docs/PROTOCOL.md](docs/PROTOCOL.md)) - draft, to be agreed by all three
- [x] Server tests (unit + integration + ThreadSanitizer/ASan clean)
- [ ] Persistent storage (replace `src/db_stub.c`, see `src/db.h`)
- [ ] CLI client
- [ ] Final design document

## Build and run

Requires gcc, make and python3 (tests only).

```bash
make                       # builds build/server
./build/server -p 5555 -u users.txt
```

Stop with Ctrl+C, SIGTERM, or type `shutdown` on the server console
(`users` lists who is online). All clients receive `Server shutting down...`.

Quick manual try without the real client:

```bash
printf 'CONNECT|alice\nUSERS\nQUIT\n' | nc localhost 5555
```

## Tests

```bash
make test     # C unit tests + end-to-end tests against the real server
make tsan     # everything under ThreadSanitizer
make asan     # everything under AddressSanitizer + UBSan
```

## Layout

```
src/        server sources (see docs/DESIGN-server.md)
tests/      C unit tests and integration.py
docs/       PROTOCOL.md, DESIGN-server.md, PLAN.md
users.txt   valid users
```
