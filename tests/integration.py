#!/usr/bin/env python3
"""End-to-end tests: spawn the real server binary and talk to it over TCP."""
import os
import queue
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SERVER = os.path.join(ROOT, "build", "server")
USERS = os.path.join(ROOT, "users.txt")
MAX_CLIENTS = 64


class Server:
    """Runs build/server on an ephemeral port with its own private data file
    (so tests never see each other's messages). Pass data= to reuse a file
    across restarts."""

    def __init__(self, data=None):
        self._tmp = None
        if data is None:
            self._tmp = tempfile.TemporaryDirectory()
            data = os.path.join(self._tmp.name, "messages.dat")
        self.data = data
        self.proc = subprocess.Popen(
            [SERVER, "-p", "0", "-u", USERS, "-d", data], cwd=ROOT,
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, bufsize=1)
        self.log = []
        first = self.proc.stdout.readline()
        self.log.append(first)
        # "[hh:mm:ss] Listening on port N (...)"
        self.port = int(first.split("port ")[1].split()[0])
        self._drainer = threading.Thread(target=self._drain, daemon=True)
        self._drainer.start()

    def _drain(self):
        for line in self.proc.stdout:
            self.log.append(line)

    def _finish(self):
        self._drainer.join(3)
        if not self.proc.stdout.closed:
            self.proc.stdout.close()
        if self._tmp:
            self._tmp.cleanup()
            self._tmp = None

    def stop(self, sig=signal.SIGINT, timeout=8):
        if self.proc.poll() is None:
            self.proc.send_signal(sig)
        try:
            code = self.proc.wait(timeout)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            raise
        self._finish()
        return code

    def kill(self):
        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()
        self._finish()


class Client:
    """Raw protocol client with a background reader feeding a queue."""

    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.q = queue.Queue()
        self.closed = threading.Event()
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        buf = b""
        try:
            while True:
                data = self.sock.recv(4096)
                if not data:
                    break
                buf += data
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    self.q.put(line.decode().rstrip("\r"))
        except OSError:
            pass
        self.closed.set()

    def send(self, line):
        self.sock.sendall(line.encode() + b"\n")

    def get(self, timeout=3):
        return self.q.get(timeout=timeout)

    def expect(self, prefix, timeout=3):
        line = self.get(timeout)
        assert line.startswith(prefix), f"expected {prefix!r}, got {line!r}"
        return line

    def login(self, name):
        self.send(f"CONNECT|{name}")
        return self.expect("OK|Welcome")

    def drain(self, wait=0.2):
        out = []
        time.sleep(wait)
        while True:
            try:
                out.append(self.q.get_nowait())
            except queue.Empty:
                return out

    def close(self):
        try:
            self.sock.shutdown(socket.SHUT_RDWR)   # really send FIN even though
        except OSError:                            # the reader thread holds the fd
            pass
        try:
            self.sock.close()
        except OSError:
            pass


class Base(unittest.TestCase):
    def setUp(self):
        self.server = Server()
        self.clients = []

    def tearDown(self):
        for c in self.clients:
            c.close()
        self.server.kill()

    def client(self, name=None):
        c = Client(self.server.port)
        self.clients.append(c)
        if name:
            c.login(name)
        return c

    def wait_log(self, text, timeout=3):
        end = time.time() + timeout
        while time.time() < end:
            if any(text in l for l in self.server.log):
                return True
            time.sleep(0.05)
        return False


class TestConnection(Base):
    def test_valid_login_and_welcome(self):
        c = self.client()
        self.assertIn("Welcome alice", c.login("alice"))
        self.assertTrue(self.wait_log("user alice connected"))

    def test_invalid_user_rejected_and_connection_closed(self):
        c = self.client()
        c.send("CONNECT|mallory")
        c.expect("ERR|Invalid username")
        self.assertTrue(c.closed.wait(2))

    def test_malformed_login_rejected(self):
        for bad in ["hello", "SEND|bob|hi", "CONNECT|", "CONNECT|a b", "CONNECT|../etc"]:
            c = self.client()
            c.send(bad)
            self.assertTrue(c.get().startswith("ERR"), bad)
            self.assertTrue(c.closed.wait(2), bad)

    def test_duplicate_session_rejected(self):
        first = self.client("alice")
        second = self.client()
        second.send("CONNECT|alice")
        second.expect("ERR|User already connected")
        self.assertTrue(second.closed.wait(2))
        # first session is unaffected
        first.send("USERS")
        self.assertIn("alice", first.expect("USERLIST|"))

    def test_relogin_after_disconnect(self):
        a = self.client("alice")
        a.send("QUIT")
        a.expect("OK|Goodbye")
        self.assertTrue(a.closed.wait(2))
        self.assertTrue(self.wait_log("user alice disconnected"))
        b = self.client("alice")             # must succeed now
        b.send("USERS")
        self.assertIn("alice", b.expect("USERLIST|"))

    def test_abrupt_disconnect_removes_user(self):
        a = self.client("alice")
        b = self.client("bob")
        a.close()
        self.assertTrue(self.wait_log("user alice disconnected"))
        b.send("USERS")
        users = b.expect("USERLIST|").split("|", 1)[1].split(",")
        self.assertEqual(users, ["bob"])

    def test_five_plus_concurrent_clients(self):
        names = ["alice", "bob", "carol", "dave", "eve", "frank", "grace", "heidi"]
        cs = [self.client(n) for n in names]
        cs[0].send("USERS")
        users = cs[0].expect("USERLIST|").split("|", 1)[1].split(",")
        self.assertEqual(sorted(users), sorted(names))

    def test_server_full(self):
        socks = []
        try:
            for _ in range(MAX_CLIENTS):
                socks.append(socket.create_connection(("127.0.0.1", self.server.port)))
            time.sleep(0.3)
            extra = Client(self.server.port)
            self.clients.append(extra)
            extra.expect("ERR|Server full")
            self.assertTrue(extra.closed.wait(2))
        finally:
            for s in socks:
                s.close()
        # slots are reclaimed once the idle connections drop
        time.sleep(0.5)
        self.client("alice")


class TestMessaging(Base):
    def test_live_delivery(self):
        a, b = self.client("alice"), self.client("bob")
        a.send("SEND|bob|hello bob")
        self.assertEqual(b.get(), "MSG|alice|hello bob")
        a.expect("OK|Delivered to bob")

    def test_invalid_recipient(self):
        a = self.client("alice")
        a.send("SEND|nobody|hi")
        a.expect("ERR|Invalid user")
        a.send("SEND|alice|to myself")
        a.expect("ERR")

    def test_offline_message_delivered_at_next_login_once(self):
        a = self.client("alice")
        a.send("SEND|bob|first")
        a.expect("OK|bob is offline")
        a.send("SEND|bob|second | with pipe")
        a.expect("OK|bob is offline")

        b = self.client()
        b.send("CONNECT|bob")
        self.assertIn("(2 unread)", b.expect("OK|Welcome"))
        m1 = b.expect("UNREAD|alice|")
        m2 = b.expect("UNREAD|alice|")
        self.assertTrue(m1.endswith("|first"))
        self.assertTrue(m2.endswith("|second | with pipe"))
        b.send("QUIT")
        b.expect("OK|Goodbye")
        self.assertTrue(b.closed.wait(2))

        b2 = self.client()
        b2.send("CONNECT|bob")
        self.assertIn("(0 unread)", b2.expect("OK|Welcome"))
        self.assertEqual(b2.drain(), [])

    def test_history_and_delete(self):
        a, b = self.client("alice"), self.client("bob")
        a.send("SEND|bob|m1"); a.expect("OK"); b.expect("MSG")
        b.send("SEND|alice|m2"); b.expect("OK"); a.expect("MSG")
        a.send("GETMSGS|bob")
        h1 = a.expect("HIST|alice|bob|")
        h2 = a.expect("HIST|bob|alice|")
        self.assertTrue(h1.endswith("|m1") and h2.endswith("|m2"))
        a.expect("OK|2 message(s)")

        a.send("GETMSGS|nobody"); a.expect("ERR|Invalid user")
        a.send("DELMSGS|nobody"); a.expect("ERR|Invalid user")
        a.send("DELMSGS|bob");    a.expect("OK|Deleted 2")
        a.send("GETMSGS|bob");    a.expect("OK|0 message(s)")
        b.send("GETMSGS|alice");  b.expect("OK|0 message(s)")   # both sides gone

    def test_malformed_commands_do_not_crash_server(self):
        a = self.client("alice")
        for bad in ["", "|", "||", "SEND", "SEND|", "SEND||", "GETMSGS", "DELMSGS|",
                    "bogus", "SEND|bob", "\x00\x01\x02", "CONNECT|alice"]:
            a.send(bad)
            self.assertTrue(a.get().startswith("ERR"), repr(bad))
        a.send("USERS")
        a.expect("USERLIST|")

    def test_overlong_line_closes_connection_server_survives(self):
        a = self.client("alice")
        a.sock.sendall(b"x" * 10000 + b"\n")
        a.expect("ERR|Line too long")
        self.assertTrue(a.closed.wait(2))
        b = self.client("bob")
        b.send("USERS"); b.expect("USERLIST|")

    def test_overlong_message_text_rejected(self):
        a, _ = self.client("alice"), self.client("bob")
        a.send("SEND|bob|" + "y" * 1500)
        a.expect("ERR|Message too long")

    def test_crlf_clients_supported(self):
        c = self.client()
        c.sock.sendall(b"CONNECT|alice\r\n")
        c.expect("OK|Welcome")
        c.sock.sendall(b"USERS\r\n")
        c.expect("USERLIST|")

    def test_split_packets(self):
        c = self.client()
        for chunk in (b"CONN", b"ECT|al", b"ice\nUSE", b"RS\n"):
            c.sock.sendall(chunk)
            time.sleep(0.05)
        c.expect("OK|Welcome")
        c.expect("USERLIST|")


class TestConcurrency(Base):
    def test_ring_stress_no_loss_no_corruption(self):
        names = ["alice", "bob", "carol", "dave", "eve", "frank", "grace", "heidi"]
        per = 60
        cs = [self.client(n) for n in names]
        errors = []

        def sender(i):
            me, nxt = names[i], names[(i + 1) % len(names)]
            for k in range(per):
                cs[i].send(f"SEND|{nxt}|{me}:{k}")

        threads = [threading.Thread(target=sender, args=(i,)) for i in range(len(names))]
        for t in threads: t.start()
        for t in threads: t.join()

        for i, c in enumerate(cs):
            prev = names[(i - 1) % len(names)]
            got = []
            oks = 0
            deadline = time.time() + 10
            while (len(got) < per or oks < per) and time.time() < deadline:
                try:
                    line = c.get(timeout=1)
                except queue.Empty:
                    continue
                if line.startswith("MSG|"):
                    got.append(line)
                elif line.startswith("OK|"):
                    oks += 1
                else:
                    errors.append(line)
            self.assertEqual(oks, per, f"{names[i]} acks")
            self.assertEqual(got, [f"MSG|{prev}|{prev}:{k}" for k in range(per)],
                             f"{names[i]} must receive all, in order")
        self.assertEqual(errors, [])

    def test_concurrent_logins_same_name_single_winner(self):
        results = []
        lock = threading.Lock()

        def attempt():
            c = Client(self.server.port)
            self.clients.append(c)
            c.send("CONNECT|alice")
            r = c.get()
            with lock:
                results.append(r)

        ts = [threading.Thread(target=attempt) for _ in range(12)]
        for t in ts: t.start()
        for t in ts: t.join()
        self.assertEqual(sum(r.startswith("OK|Welcome") for r in results), 1)
        self.assertEqual(sum(r == "ERR|User already connected" for r in results), 11)

    def test_message_during_login_never_lost(self):
        """A sender racing the receiver's login must land exactly once: either
        as a live MSG or as an UNREAD at login - never dropped or duplicated."""
        a = self.client("alice")
        n = 40
        got = []
        stop = threading.Event()

        def spam():
            for k in range(n):
                a.send(f"SEND|bob|race-{k}")
                time.sleep(0.002)
            stop.set()

        t = threading.Thread(target=spam)
        t.start()
        time.sleep(0.03)
        b = self.client()
        b.send("CONNECT|bob")
        t.join()
        time.sleep(0.5)
        for line in b.drain(0.3):
            if line.startswith(("MSG|", "UNREAD|")):
                got.append(line.rsplit("|", 1)[1])
        self.assertEqual(sorted(got, key=lambda s: int(s.split("-")[1])),
                         [f"race-{k}" for k in range(n)])


class TestShutdown(Base):
    def _shutdown_with_clients(self, sig):
        cs = [self.client(n) for n in ["alice", "bob", "carol", "dave", "eve"]]
        idle = Client(self.server.port)        # connected but not logged in
        self.clients.append(idle)
        time.sleep(0.2)
        code = self.server.stop(sig)
        self.assertEqual(code, 0)
        for c in cs:
            self.assertEqual(c.get(), "SHUTDOWN|Server shutting down...")
            self.assertTrue(c.closed.wait(3), "client must see connection close")
        self.assertTrue(idle.closed.wait(3))
        out = "".join(self.server.log)
        self.assertIn("Server shutting down...", out)
        self.assertIn("Shutdown complete", out)

    def test_sigint_broadcast_and_clean_exit(self):
        self._shutdown_with_clients(signal.SIGINT)

    def test_sigterm_broadcast_and_clean_exit(self):
        self._shutdown_with_clients(signal.SIGTERM)

    def test_shutdown_under_load(self):
        names = ["alice", "bob", "carol", "dave"]
        cs = [self.client(n) for n in names]
        stop = threading.Event()

        def noisy(i):
            while not stop.is_set():
                try:
                    cs[i].send(f"SEND|{names[(i + 1) % 4]}|x")
                except OSError:
                    return

        ts = [threading.Thread(target=noisy, args=(i,)) for i in range(4)]
        for t in ts: t.start()
        time.sleep(0.3)
        code = self.server.stop(signal.SIGINT)
        stop.set()
        for t in ts: t.join()
        self.assertEqual(code, 0)
        for c in cs:
            self.assertTrue(c.closed.wait(3))

    def test_clients_exit_when_server_killed_mid_session(self):
        a = self.client("alice")
        self.server.kill()
        self.assertTrue(a.closed.wait(3))

    def test_server_log_shows_events(self):
        a = self.client("alice")
        a.send("QUIT"); a.expect("OK|Goodbye")
        self.assertTrue(self.wait_log("user alice connected"))
        self.assertTrue(self.wait_log("user alice disconnected"))


if __name__ == "__main__":
    if not os.path.exists(SERVER):
        sys.exit("build the server first: make")
    unittest.main(verbosity=2)
