#!/usr/bin/env python3
"""Generate the PNG diagrams used by the design document into docs/images/."""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "docs", "images")
os.makedirs(OUT, exist_ok=True)

BLUE, GREEN, YELLOW, GREY, RED, PURPLE = "#DDEBF7", "#E2EFDA", "#FFF2CC", "#EDEDED", "#FCE4D6", "#E4DFEC"
EDGE = "#44546A"


def canvas(w, h):
    fig, ax = plt.subplots(figsize=(w, h))
    ax.set_xlim(0, w * 10)
    ax.set_ylim(0, h * 10)
    ax.axis("off")
    return fig, ax


def box(ax, x, y, w, h, text, color=GREY, fs=9, bold=False, ls="-", ec=EDGE):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.3,rounding_size=1.2",
                                fc=color, ec=ec, lw=1.3, ls=ls))
    ax.text(x + w / 2, y + h / 2, text, ha="center", va="center", fontsize=fs,
            fontweight="bold" if bold else "normal", wrap=True)


def arrow(ax, p1, p2, text="", color=EDGE, ls="-", fs=8, both=False, off=(0, 1.2), rad=0.0):
    a = FancyArrowPatch(p1, p2, arrowstyle="<->" if both else "->", mutation_scale=12,
                        color=color, lw=1.4, ls=ls, connectionstyle=f"arc3,rad={rad}")
    ax.add_patch(a)
    if text:
        ax.text((p1[0] + p2[0]) / 2 + off[0], (p1[1] + p2[1]) / 2 + off[1], text,
                ha="center", va="center", fontsize=fs, color=color,
                bbox=dict(fc="white", ec="none", pad=0.6, alpha=0.9))


def save(fig, name):
    fig.savefig(os.path.join(OUT, name), dpi=170, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    print("wrote", name)


# ---------------------------------------------------------------- architecture
def architecture():
    fig, ax = canvas(13, 9.2)
    ax.text(65, 89, "System architecture: client / server interaction, threads and data flow",
            ha="center", fontsize=12, fontweight="bold")
    rows = [56, 31, 6]
    for name, y in zip(["alice", "bob", "carol"], rows):
        box(ax, 2, y, 26, 21, "", BLUE)
        ax.text(15, y + 18, f"Client process: {name}", ha="center", fontsize=9, fontweight="bold")
        box(ax, 4, y + 8.5, 22, 5, "main thread: read stdin,\nparse command, send", "white", 7.5)
        box(ax, 4, y + 1.5, 22, 5, "receiver thread: recv(),\nprint messages live", "white", 7.5)

    box(ax, 40, 3, 70, 74, "", YELLOW)
    ax.text(75, 73.5, "Server process (one process, many threads)", ha="center", fontsize=9.5, fontweight="bold")
    for nm, y in zip("ABC", rows):
        box(ax, 43, y + 4, 24, 13, f"client_thread {nm}\nlr_read -> do_login\n-> cmd_handle", GREEN, 7.2)
        arrow(ax, (28, y + 10.5), (43, y + 10.5), "TCP lines" if nm == "A" else "", both=True, off=(0, 2.4))

    box(ax, 73, 58, 34, 11, "main thread\npoll(listen, signal pipe, stdin)\naccept -> session_open\n-> pthread_create", "white", 7)
    box(ax, 73, 42, 34, 12, "session table (64 slots)\ntable_lock + per-slot wlock\nlogin / send / list / broadcast", PURPLE, 7.2)
    box(ax, 73, 28, 34, 10, "commands.c\nSEND GETMSGS DELMSGS USERS", "white", 7.2)
    box(ax, 73, 14, 34, 10, "db_file.c  [db mutex]\nin-memory array + log file", "white", 7.2)
    box(ax, 73, 5.5, 34, 5.5, "users.c: valid-user list", "white", 7.2)

    # threads -> shared modules
    ax.add_patch(FancyBboxPatch((69.3, 8), 1.6, 58, boxstyle="round,pad=0.1", fc="#C9C9C9", ec=EDGE, lw=1))
    for y in rows:
        arrow(ax, (67, y + 10.5), (69.3, y + 10.5), "")
    ax.text(70.1, 37, "calls", rotation=90, ha="center", va="center", fontsize=7.5)
    for y in (63.5, 48, 33, 19, 8.2):
        arrow(ax, (70.9, y), (73, y), "")

    box(ax, 114, 30, 15, 16, "messages.dat\nappend-only log\nfsync + atomic\nrewrite", GREY, 7.3)
    box(ax, 114, 8, 15, 10, "users.txt\nvalid users", GREY, 7.3)
    arrow(ax, (107, 19), (114, 36), "", both=True)
    arrow(ax, (107, 8.2), (114, 12), "load once")
    box(ax, 40, 79.5, 70, 3.6, "SIGINT / SIGTERM / console 'shutdown'  ->  wake pipe  ->  graceful shutdown broadcast", RED, 7.5)
    save(fig, "architecture.png")


# ------------------------------------------------------------ sequence diagrams
def sequence(name, title, parts, msgs, w=13, step=6.3, notes=()):
    h = 2.3 + len(msgs) * step / 10 + 0.4
    fig, ax = canvas(w, h)
    H = h * 10
    ax.text(w * 5, H - 2, title, ha="center", fontsize=11.5, fontweight="bold")
    n = len(parts)
    lifelines = []
    xs = [w * 10 * (i + 0.5) / n for i in range(n)]
    for x, p in zip(xs, parts):
        box(ax, x - 9, H - 12, 18, 6, p, BLUE, 8, bold=True)
        lifelines.append(x)
    y = H - 17
    for m in msgs:
        if m[0] == "note":
            _, idx, text = m
            ax.text(xs[idx], y, text, ha="center", va="center", fontsize=7.4, color="#7F6000",
                    bbox=dict(fc=YELLOW, ec="#BF9000", boxstyle="round,pad=0.3"))
        else:
            a, b, text = m[:3]
            dashed = len(m) > 3 and m[3]
            if a == b:
                last = a == n - 1
                d = -3.5 if last else 3.5
                ax.plot([xs[a], xs[a] + d, xs[a] + d], [y + 1.1, y + 1.1, y - 1.1], color=EDGE, lw=1.4)
                arrow(ax, (xs[a] + d, y - 1.1), (xs[a], y - 1.1), "")
                ax.text(xs[a] + d + (-1 if last else 1), y, text, ha="right" if last else "left",
                        va="center", fontsize=7.8, color="#1F3864")
            else:
                arrow(ax, (xs[a], y), (xs[b], y), "", ls="--" if dashed else "-")
                ax.text((xs[a] + xs[b]) / 2, y + 1.55, text, ha="center", va="bottom", fontsize=7.8)
        y -= step
    for x in lifelines:
        ax.plot([x, x], [y + step - 3, H - 12.5], color="#8497B0", lw=1, ls="--", zorder=0)
    save(fig, name)


def sequences():
    sequence("seq_login.png", "Login: authentication, duplicate check and unread delivery",
             ["Client", "client_thread", "session table", "storage (db)"],
             [(0, 1, "TCP connect  (accept -> session_open, thread created)"),
              (0, 1, "CONNECT|alice"),
              (1, 1, "users_name_wellformed + users_is_valid"),
              ("note", 1, "take delivery_lock"),
              (1, 2, "session_login('alice')"),
              (2, 1, "0 ok   (-1 => ERR|User already connected, close)", True),
              (1, 3, "db_take_unread('alice')"),
              (3, 1, "n unread messages (marked delivered)", True),
              (1, 0, "OK|Welcome alice (n unread)", True),
              (1, 0, "UNREAD|from|ts|text   x n", True),
              ("note", 1, "release delivery_lock"),
              (0, 1, "... command loop begins ...")], step=6.6)

    sequence("seq_send.png", "Sending a message: live delivery and offline queueing",
             ["alice (client)", "alice's thread", "session table", "storage (db)", "bob (client)"],
             [(0, 1, "SEND|bob|hello"),
              (1, 1, "validate: bob valid, not self, text 1..1024"),
              ("note", 1, "take delivery_lock"),
              (1, 2, "session_send_user('bob', MSG|alice|hello)"),
              (2, 4, "write line under bob's wlock  (if bob is online)", True),
              (2, 1, "0 = delivered, -1 = bob offline / write failed", True),
              (1, 3, "db_save(alice, bob, hello, delivered)"),
              (3, 3, "append + fsync, then acknowledge"),
              ("note", 1, "release delivery_lock"),
              (1, 0, "OK|Delivered to bob   /   OK|bob is offline, message stored", True),
              (4, 4, "prints  'Message from alice: hello'")], step=6.6)

    sequence("seq_shutdown.png", "Server shutdown (Ctrl+C, SIGTERM or console command)",
             ["Operator", "signal handler", "main thread", "client threads", "Clients"],
             [(0, 1, "SIGINT / SIGTERM"),
              (1, 2, "write 1 byte to wake pipe (async-signal-safe)"),
              (2, 2, "poll() returns, leave accept loop"),
              (2, 2, "close(listen fd): no new connections"),
              (2, 4, "session_broadcast  SHUTDOWN|Server shutting down...", True),
              (2, 3, "session_shutdown_all: shutdown(fd, SHUT_RDWR)"),
              (3, 3, "recv() returns EOF -> session_close()"),
              (3, 4, "sockets closed", True),
              (4, 4, "print message, exit(0)"),
              (3, 2, "session_wait_empty() wakes: table empty", True),
              (2, 2, "db_close() (fsync) ; exit(0)")], step=6.6)

    sequence("seq_client_flow.png", "Client: two-thread operation",
             ["User (stdin)", "main thread", "receiver thread", "Server"],
             [(0, 1, "connect 127.0.0.1 alice"),
              (1, 3, "TCP connect, send CONNECT|alice"),
              (3, 1, "OK|Welcome alice (1 unread) + UNREAD lines", True),
              (1, 2, "pthread_create(receiver)"),
              (0, 1, "sendmessage bob hi"),
              (1, 3, "SEND|bob|hi"),
              (3, 2, "OK|Delivered to bob", True),
              (2, 2, "prints reply immediately"),
              (3, 2, "MSG|carol|ping   (arrives while user is idle/typing)", True),
              (2, 2, "prints 'Message from carol: ping'"),
              (0, 1, "shutdown"),
              (1, 3, "QUIT"),
              (3, 2, "OK|Goodbye, then EOF", True),
              (2, 1, "wake pipe: receiver finished"),
              (1, 1, "join thread, close socket, exit(0)")], step=6.2)


# --------------------------------------------------------------- locks / threads
def locks():
    fig, ax = canvas(12, 6.4)
    ax.text(60, 61, "Synchronisation design: shared data, locks and lock order", ha="center",
            fontsize=12, fontweight="bold")
    box(ax, 3, 36, 32, 14, "delivery_lock\n(1 global mutex)\nmakes  'is bob online? + store'\natomic vs  'login + fetch unread'", YELLOW, 8, True)
    box(ax, 44, 36, 32, 14, "table_lock\n(1 global mutex)\nprotects session table:\nstate, name, fd, used_slots", GREEN, 8, True)
    box(ax, 85, 36, 32, 14, "wlock  (one per session)\nserialises writes to one\nsocket so lines never\ninterleave", BLUE, 8, True)
    arrow(ax, (35, 43), (44, 43), "then", fs=9)
    arrow(ax, (76, 43), (85, 43), "then", fs=9)
    ax.text(60, 54, "ALWAYS acquired in this order  ->  no circular wait  ->  no deadlock", ha="center",
            fontsize=9.5, color="#C00000", fontweight="bold")

    box(ax, 3, 10, 36, 18, "db mutex  (inside db_file.c)\nprotects the in-memory array,\nnext_id, and the log file handle.\nNever held while taking another\nlock of ours (leaf lock).", PURPLE, 8)
    box(ax, 44, 10, 32, 18, "log_lock  (log.c)\nkeeps one log line whole.\nLeaf lock.\n\nusers list: read-only after\nstartup -> needs no lock", GREY, 8)
    box(ax, 85, 10, 32, 18, "table_empty  (condition variable)\nsession_wait_empty() sleeps\nhere; session_close() signals\nwhen the last slot is freed", GREY, 8)
    save(fig, "locks.png")


# ----------------------------------------------------------------- data structs
def datastructs():
    fig, ax = canvas(13, 7.4)
    ax.text(65, 71, "Server data structures and on-disk format", ha="center", fontsize=12, fontweight="bold")
    box(ax, 3, 38, 58, 28, "", BLUE)
    ax.text(32, 63, "session_t table[MAX_CLIENTS = 64]   (in memory)", ha="center", fontsize=9, fontweight="bold")
    for i, (st, nm) in enumerate([("LOGGED_IN", "alice"), ("CONNECTED", "(no name yet)"), ("LOGGED_IN", "bob"), ("FREE", "-")]):
        box(ax, 6 + i * 13.7, 41, 12.6, 18, f"slot {i}\nstate={st}\nname={nm}\nfd, addr,\nwlock", GREEN if st == "LOGGED_IN" else GREY, 6.3)
    box(ax, 66, 38, 60, 28, "", YELLOW)
    ax.text(96, 63, "rec_t recs[]   (storage working set, growable array)", ha="center", fontsize=9, fontweight="bold")
    for i, t in enumerate(["id=1\nalice->bob\ndelivered=1\n'hi'", "id=2\nbob->alice\ndelivered=1\n'hello'", "id=3\ncarol->bob\ndelivered=0\n'call me'"]):
        box(ax, 69 + i * 19.3, 41, 17.6, 18, t, "white", 6.8)

    box(ax, 3, 4, 123, 27, "", GREY)
    ax.text(64, 28, "messages.dat   (append-only text log, one record per line, TAB separated; file mode 0600)", ha="center", fontsize=9, fontweight="bold")
    lines = ["M <TAB> 1 <TAB> 1763404501 <TAB> 1 <TAB> alice <TAB> bob   <TAB> hi",
             "M <TAB> 2 <TAB> 1763404503 <TAB> 1 <TAB> bob   <TAB> alice <TAB> hello",
             "M <TAB> 3 <TAB> 1763404509 <TAB> 0 <TAB> carol <TAB> bob   <TAB> call me",
             "V <TAB> 3                       <- 'message 3 was delivered' (appended when bob logs in)"]
    for i, l in enumerate(lines):
        ax.text(6, 22 - i * 4.4, l, family="monospace", fontsize=7.6, va="center")
    ax.text(6, 5.5, "fields: type  id  unix-time  delivered  from  to  escaped-text      escapes:  \\ -> \\\\   newline -> \\n   TAB -> \\t   CR -> \\r",
            fontsize=7.2, color="#44546A")
    arrow(ax, (96, 38), (96, 31), "append + fsync / atomic rewrite on delete", both=True, off=(30, 0))
    save(fig, "datastructures.png")


def threads():
    fig, ax = canvas(12, 5.6)
    ax.text(60, 53, "Threading model", ha="center", fontsize=12, fontweight="bold")
    box(ax, 3, 28, 28, 18, "MAIN THREAD\nsingle: accept loop only\npoll(listen, wake pipe, stdin)\ncreates detached threads", YELLOW, 8, True)
    for i in range(3):
        box(ax, 44 + i * 25, 28, 22, 18, f"client_thread #{i + 1}\nowns 1 socket\nblocking recv, one\nline at a time", GREEN, 8)
        arrow(ax, (31, 37), (44 + i * 25, 37), "pthread_create" if i == 0 else "", fs=7, off=(0, 3 + i * 3))
    box(ax, 3, 4, 117, 17, "Why thread-per-client?  A blocked recv() on one slow client costs one thread, never stalls the others; "
        "code per client stays straight-line (read line -> handle -> reply).\n"
        "Why poll() in the main thread?  accept(), the signal wake-pipe and the console must be waited for in one place "
        "(I/O multiplexing), so Ctrl+C is noticed immediately.", GREY, 7.8)
    save(fig, "threads.png")


if __name__ == "__main__":
    architecture()
    sequences()
    locks()
    datastructs()
    threads()
