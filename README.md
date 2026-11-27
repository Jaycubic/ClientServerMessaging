# Multi-User Client-Server Messaging Application

OS group project (C, Linux): a multi-threaded messaging server and a CLI client
demonstrating processes, threads, mutex synchronisation, socket IPC,
`poll()` I/O multiplexing, persistent storage and signal handling.

| Member | Part |
|---|---|
| Jofrey | Server core & concurrency (`server.c`, `net.c`, `session.c`, `client_handler.c`) |
| Ibrahim | Storage & message logic (`db.h`, `db_file.c`, `users.c`, `commands.c`) |
| Krishang | Client, protocol & tests (`client/`, `docs/PROTOCOL.md`, `tests/`) |

## Quick start

```bash
make                          # builds build/server and build/client (gcc, make only)
./build/server -p 5555        # terminal 1
./build/client                # terminals 2..n
> connect 127.0.0.1 alice
> sendmessage bob hello
> getmessages bob
> deletemessages bob
> getuserlist
> shutdown
```

Valid users are listed in `users.txt`. Stop the server with Ctrl+C (all clients are
told "Server shutting down..." and exit cleanly).

## Tests

```bash
make test     # 326 C unit checks + 47 end-to-end tests (real server and real clients)
make tsan     # everything under ThreadSanitizer
make asan     # everything under AddressSanitizer + UBSan
```

## Documentation

| Document | Contents |
|---|---|
| [docs/Design_Document.docx](docs/Design_Document.docx) ([.md](docs/Design_Document.md)) | The submitted design document: architecture diagrams, protocol, concurrency design, storage, testing strategy |
| [docs/Complete_Documentation.docx](docs/Complete_Documentation.docx) ([.md](docs/DOCUMENTATION.md)) | User guide, file-by-file and function-by-function walkthrough, decision log, viva question bank |
| [docs/PROTOCOL.md](docs/PROTOCOL.md) | Wire protocol specification |

Regenerate the Word files and diagrams (needs `pip install python-docx matplotlib`):

```bash
python3 scripts/make_diagrams.py
python3 scripts/md2docx.py docs/Design_Document.md docs/Design_Document.docx \
    --title-page "Multi-User Client-Server Messaging Application|Design Document|Operating Systems Group Project (CSIT203)|Jofrey  -  Ibrahim  -  Krishang"
python3 scripts/md2docx.py docs/DOCUMENTATION.md docs/Complete_Documentation.docx \
    --title-page "Complete Documentation|Multi-User Client-Server Messaging Application|User guide, code walkthrough, decision log and viva preparation|Jofrey  -  Ibrahim  -  Krishang"
```

## Shared team account

`scripts/setup_csit203_user.sh` (run once as root) creates an unprivileged account
with the toolchain, this repo cloned, built and tested:

```bash
sudo CSIT203_PASSWORD='<password>' bash scripts/setup_csit203_user.sh
```

The password is intentionally not stored in the repository. The account has no
sudo rights, cannot install packages, has a private home directory and cannot use
SSH port-forwarding.

## Layout

```
src/        server sources            client/   client sources
tests/      unit + end-to-end tests   docs/     documents and diagrams
scripts/    account setup, diagrams, md -> docx      users.txt   valid users
```
