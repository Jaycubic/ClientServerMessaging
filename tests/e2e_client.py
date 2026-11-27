#!/usr/bin/env python3
"""End-to-end tests with the REAL client binary talking to the REAL server.

Every client is a separate OS process driven through its stdin/stdout, exactly
as a user would use it (minus the terminal)."""
import os
import queue
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from integration import Server, ROOT  # noqa: E402

CLIENT = os.path.join(ROOT, "build", "client")


class ClientProc:
    def __init__(self, port, args=()):
        self.proc = subprocess.Popen(
            [CLIENT, "-p", str(port), *args], cwd=ROOT,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, bufsize=1)
        self.q = queue.Queue()
        self.all = []
        self._t = threading.Thread(target=self._read, daemon=True)
        self._t.start()

    def _read(self):
        for line in self.proc.stdout:
            line = line.rstrip("\n")
            self.all.append(line)
            self.q.put(line)

    def type(self, cmd):
        self.proc.stdin.write(cmd + "\n")
        self.proc.stdin.flush()

    def expect(self, text, timeout=4):
        """Wait for an output line containing `text`; return it."""
        end = time.time() + timeout
        while True:
            left = end - time.time()
            if left <= 0:
                raise AssertionError(f"timeout waiting for {text!r}; saw {self.all}")
            try:
                line = self.q.get(timeout=left)
            except queue.Empty:
                continue
            if text in line:
                return line

    def connect(self, port_unused, user):
        self.type(f"connect 127.0.0.1 {user}")
        return self.expect(f"Welcome {user}")

    def wait_exit(self, timeout=6):
        try:
            code = self.proc.wait(timeout)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            raise AssertionError(f"client did not exit; output {self.all}")
        self._t.join(2)
        return code

    def kill(self):
        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()
        for f in (self.proc.stdin, self.proc.stdout):
            try:
                f.close()
            except Exception:
                pass


class Base(unittest.TestCase):
    def setUp(self):
        self.server = Server()
        self.clients = []

    def tearDown(self):
        for c in self.clients:
            c.kill()
        self.server.kill()

    def new(self, user=None, args=()):
        c = ClientProc(self.server.port, args)
        self.clients.append(c)
        if user:
            c.connect(self.server.port, user)
        return c

    def wait_log(self, text, timeout=4):
        end = time.time() + timeout
        while time.time() < end:
            if any(text in l for l in self.server.log):
                return True
            time.sleep(0.05)
        return False


class TestUserFlow(Base):
    def test_banner_help_and_connect(self):
        c = self.new()
        c.expect("Type 'help'")
        c.type("help")
        c.expect("sendmessage <user> <message>")
        self.assertIn("(0 unread)", c.connect(0, "alice"))
        self.assertTrue(self.wait_log("user alice connected"))

    def test_connect_via_command_line_arguments(self):
        c = self.new(args=("127.0.0.1", "alice"))
        c.expect("Welcome alice")

    def test_invalid_user_then_retry_succeeds(self):
        c = self.new()
        c.type("connect 127.0.0.1 mallory")
        c.expect("Error: Invalid username")
        c.connect(0, "alice")                       # same client process, retry works

    def test_commands_before_connect_are_refused(self):
        c = self.new()
        for cmd in ("sendmessage bob hi", "getmessages bob", "deletemessages bob", "getuserlist"):
            c.type(cmd)
            c.expect("Error: Not connected")

    def test_cannot_reach_server(self):
        c = self.new()
        c.type("connect 127.0.0.1 alice 1")         # nothing listens on port 1
        c.expect("Error: cannot connect to 127.0.0.1:1")
        c.connect(0, "alice")                       # still usable afterwards

    def test_usage_errors(self):
        c = self.new("alice")
        c.type("sendmessage bob");   c.expect("Usage: sendmessage")
        c.type("getmessages");       c.expect("Usage: getmessages")
        c.type("deletemessages");    c.expect("Usage: deletemessages")
        c.type("nonsense");          c.expect("Unknown command")
        c.type("connect 127.0.0.1 alice"); c.expect("Already connected")
        c.type("getuserlist");       c.expect("Online users (1): alice")

    def test_duplicate_session_rejected(self):
        self.new("alice")
        second = self.new()
        second.type("connect 127.0.0.1 alice")
        second.expect("Error: User already connected")
        second.connect(0, "bob")                    # a different name works

    def test_shutdown_command_exits_cleanly(self):
        c = self.new("alice")
        c.type("shutdown")
        self.assertEqual(c.wait_exit(), 0)
        self.assertTrue(self.wait_log("user alice disconnected"))

    def test_shutdown_before_connect_exits(self):
        c = self.new()
        c.type("shutdown")
        self.assertEqual(c.wait_exit(), 0)

    def test_stdin_eof_disconnects_gracefully(self):
        c = self.new("alice")
        c.proc.stdin.close()
        self.assertEqual(c.wait_exit(), 0)
        self.assertTrue(self.wait_log("user alice disconnected"))

    def test_client_sigint_disconnects_gracefully(self):
        c = self.new("alice")
        c.proc.send_signal(signal.SIGINT)
        self.assertEqual(c.wait_exit(), 0)
        self.assertTrue(self.wait_log("user alice disconnected"))


class TestMessagingFlow(Base):
    def test_realtime_delivery(self):
        a, b = self.new("alice"), self.new("bob")
        a.type("sendmessage bob hello bob, how are you?")
        b.expect("Message from alice: hello bob, how are you?")
        a.expect("Delivered to bob")

    def test_message_to_invalid_and_self(self):
        a = self.new("alice")
        a.type("sendmessage nobody hi");  a.expect("Error: Invalid user 'nobody'")
        a.type("sendmessage alice hi");   a.expect("Error: You cannot send a message to yourself")

    def test_offline_message_shown_as_unread_at_login(self):
        a = self.new("alice")
        a.type("sendmessage bob are you there?")
        a.expect("bob is offline")
        a.type("sendmessage bob please reply")
        a.expect("bob is offline")

        b = self.new()
        b.type("connect 127.0.0.1 bob")
        b.expect("Welcome bob (2 unread)")
        b.expect("Message from alice: are you there?")
        b.expect("Message from alice: please reply")
        b.type("shutdown"); self.assertEqual(b.wait_exit(), 0)

        b2 = self.new()
        b2.type("connect 127.0.0.1 bob")
        b2.expect("Welcome bob (0 unread)")         # unread shown only once

    def test_getuserlist(self):
        a = self.new("alice"); self.new("bob"); self.new("carol")
        a.type("getuserlist")
        line = a.expect("Online users (3)")
        for n in ("alice", "bob", "carol"):
            self.assertIn(n, line)

    def test_history_and_delete(self):
        a, b = self.new("alice"), self.new("bob")
        a.type("sendmessage bob first");  b.expect("first")
        b.type("sendmessage alice second"); a.expect("second")
        a.type("getmessages bob")
        a.expect("alice -> bob: first")
        a.expect("bob -> alice: second")
        a.expect("2 message(s) with bob")
        a.type("getmessages nobody"); a.expect("Error: Invalid user")
        a.type("deletemessages nobody"); a.expect("Error: Invalid user")
        a.type("deletemessages bob"); a.expect("Deleted 2 message(s) with bob")
        a.type("getmessages bob");    a.expect("0 message(s) with bob")
        b.type("getmessages alice");  b.expect("0 message(s) with alice")

    def test_message_with_special_characters(self):
        a, b = self.new("alice"), self.new("bob")
        a.type("sendmessage bob pipes | and \\ backslash \t tab 100% {x} $HOME `ls`")
        b.expect("Message from alice: pipes | and \\ backslash \t tab 100% {x} $HOME `ls`")

    def test_two_consecutive_messages_arrive_in_order(self):
        a, b = self.new("alice"), self.new("bob")
        for i in range(20):
            a.type(f"sendmessage bob msg-{i}")
        for i in range(20):
            b.expect(f"Message from alice: msg-{i}")

    def test_six_real_clients_chat_concurrently(self):
        names = ["alice", "bob", "carol", "dave", "eve", "frank"]
        cs = [self.new(n) for n in names]
        per = 15

        def sender(i):
            nxt = names[(i + 1) % len(names)]
            for k in range(per):
                cs[i].type(f"sendmessage {nxt} {names[i]}-{k}")

        ts = [threading.Thread(target=sender, args=(i,)) for i in range(len(cs))]
        for t in ts: t.start()
        for t in ts: t.join()
        for i, c in enumerate(cs):
            prev = names[(i - 1) % len(names)]
            for k in range(per):
                c.expect(f"Message from {prev}: {prev}-{k}", timeout=8)


class TestServerLifecycle(Base):
    def test_server_sigint_clients_notified_and_exit(self):
        cs = [self.new(n) for n in ("alice", "bob", "carol", "dave", "eve")]
        self.assertEqual(self.server.stop(signal.SIGINT), 0)
        for c in cs:
            c.expect("Server shutting down...")
            self.assertEqual(c.wait_exit(), 0)

    def test_clients_exit_nonzero_if_server_vanishes(self):
        c = self.new("alice")
        self.server.kill()
        c.expect("connection to server lost")
        self.assertEqual(c.wait_exit(), 1)

    def test_messages_persist_across_server_restart(self):
        d = tempfile.TemporaryDirectory()
        data = os.path.join(d.name, "persist.dat")
        self.server.kill()
        s1 = Server(data=data)
        a = ClientProc(s1.port); self.clients.append(a)
        a.connect(0, "alice")
        a.type("sendmessage bob stored on disk")
        a.expect("bob is offline")
        a.type("sendmessage carol also stored")
        a.expect("carol is offline")
        self.assertEqual(s1.stop(signal.SIGINT), 0)
        a.wait_exit()

        s2 = Server(data=data)                      # fresh process, same file
        try:
            b = ClientProc(s2.port); self.clients.append(b)
            b.type("connect 127.0.0.1 bob")
            b.expect("Welcome bob (1 unread)")
            b.expect("Message from alice: stored on disk")
            b.type("getmessages alice")
            b.expect("alice -> bob: stored on disk")
            b.type("deletemessages alice"); b.expect("Deleted 1")
            s2.stop(signal.SIGINT)
            b.wait_exit()
        finally:
            s2.kill()

        s3 = Server(data=data)                      # delete + delivered flags persisted
        try:
            c = ClientProc(s3.port); self.clients.append(c)
            c.type("connect 127.0.0.1 bob")
            c.expect("Welcome bob (0 unread)")
            c.type("getmessages alice"); c.expect("0 message(s)")
            c2 = ClientProc(s3.port); self.clients.append(c2)
            c2.type("connect 127.0.0.1 carol")
            c2.expect("Welcome carol (1 unread)")   # untouched message still there
        finally:
            s3.kill()
            d.cleanup()


if __name__ == "__main__":
    if not (os.path.exists(CLIENT) and os.path.exists(os.path.join(ROOT, "build", "server"))):
        sys.exit("build first: make")
    unittest.main(verbosity=2)
