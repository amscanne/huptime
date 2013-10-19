"""
Servers.

This file contains various server implementations,
which can be created by different tests.
"""

import os
import sys
import socket
import signal
import thread
import threading
import traceback
import select
import errno

DEFAULT_HOST = ""
DEFAULT_PORT = 7869
DEFAULT_BACKLOG = 1
DEFAULT_N = 8

class Server(object):

    """
    A generic server model.

    This server exposes a simple run() method, which
    will bind() and listen() to a socket, then accept()
    and and serve clients individually.
    """

    def __init__(self, cookie):
        super(Server, self).__init__()
        self._sock = socket.socket()
        self._cookie = cookie
        self._cond = threading.Condition()
        self._clients = 0
        assert self._cookie

    def bind(self, host=DEFAULT_HOST, port=DEFAULT_PORT):
        sys.stderr.write("%s: bind()\n" % self)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.bind((host, port))

    def listen(self, backlog=DEFAULT_BACKLOG):
        sys.stderr.write("%s: listen()\n" % self)
        self._sock.listen(backlog)

    def accept(self):
        sys.stderr.write("%s: accept()\n" % self)
        client, _ = self._sock.accept()
        self._cond.acquire()
        try:
            self._clients += 1
        finally:
            self._cond.release()
        return client

    def restart_pid(self):
        sys.stderr.write("%s: restart_pid()\n" % self)
        return self.getpid()

    def getpid(self):
        sys.stderr.write("%s: getpid()\n" % self)
        return os.getpid()

    def close(self):
        sys.stderr.write("%s: close()\n" % self)
        self._sock.close()

    def exit(self):
        sys.stderr.write("%s: exit()\n" % self)
        self.close()
        os._exit(0)

    def _handle(self, client):
        # This implements a very simple protocol that
        # allows us to test for a code "version" (by the
        # cookie at startup) and liveness (via ping).
        # The corresponding client code is available in
        # client.py.
        rval = False
        command = client.recv(1024)
        sys.stderr.write("%s: recv(%d) => %s\n" %
            (self, client.fileno(), command))
        if not command:
            # Bad client?
            self.drop(client)
            return False

        # Ensure it's a valid command.
        assert command in ["cookie", "ping", "drop"]

        if command == "cookie":
            client.send(self._cookie)
            rval = True
        elif command == "ping":
            client.send("pong")
            rval = True
        elif command == "drop":
            client.send("okay")
            self.drop(client)

        return rval

    def drop(self, client):
        self._cond.acquire()
        try:
            sys.stderr.write("%s: close(%d)\n" % 
                (self, client.fileno()))
            client.close()
            self._clients -= 1
            self._cond.notifyAll()
        finally:
            self._cond.release()

    def wait(self):
        self._cond.acquire()
        try:
            while self._clients > 0:
                sys.stderr.write("%s: waiting for clients...\n" % self)
                self._cond.wait()
        finally:
            self._cond.release()

    def handle(self, client):
        try:
            sys.stderr.write("%s: handle(%d) (%d active)\n" %
                (self, client.fileno(), self._clients))
            return self._handle(client)
        except socket.error as e:
            traceback.print_exc()
            if e.args[0] in (errno.EINTR, errno.EAGAIN):
                return True
            else:
                self.drop(client)
                return False

    def run(self):
        sys.stderr.write("%s: run() finished.\n")

    def __str__(self):
        return "%s %d.%d" % (
            self.__class__.__name__,
            os.getpid(),
            thread.get_ident())

class SimpleServer(Server):

    def run(self):
        while True:
            client = self.accept()
            while self.handle(client):
                # Continue until finished.
                pass
        super(SimpleServer, self).run()

class EventServer(Server):
   
    def run(self):
        self._fdmap = {self._sock.fileno(): self._sock}
        while True:
            rfds, wfds, efds = select.select(self._fdmap.keys(), [], [])
            for fd in rfds:
                sock = self._fdmap.get(fd)
                if sock == self._sock:
                    # Accept the client.
                    client = self.accept()
                    self._fdmap[client.fileno()] = client
                else:
                    # Process the request.
                    if not self.handle(sock):
                        del self._fdmap[fd]
        super(EventServer, self).run()

class ThreadServer(Server):

    def run(self):
        while True:
            client = self.accept()
            # Fire a thread to handle it.
            def closure(c):
                def fn():
                    sys.stderr.write("%s: thread_start()\n" % self)
                    try:
                        while self.handle(c):
                            pass
                    except:
                        traceback.print_exc()
                    sys.stderr.write("%s: thread_exit()\n" % self)
                return fn
            t = threading.Thread(target=closure(client))
            t.daemon = True
            t.start()
        super(ThreadServer, self).run()

class ProcessServer(Server):

    def __init__(self, *args, **kwargs):
        super(ProcessServer, self).__init__(*args, **kwargs)
        self._pids = []
        self._cond = threading.Condition()

    def exit(self):
        self._cond.acquire()
        for pid in self._pids:
            os.kill(pid, signal.SIGTERM)
        for pid in self._pids:
            os.waitpid(pid, 0)
        super(ProcessServer, self).exit()
        self._cond.release()

    def restart_pid(self):
        self._cond.acquire()
        for pid in self._pids:
            os.kill(pid, signal.SIGHUP)
        for pid in self._pids:
            os.waitpid(pid, 0)
        self._pids = []
        rval = super(ProcessServer, self).restart_pid()
        self._cond.release()
        return rval

class PoolServer(SimpleServer):

    def _create(self, target):
        raise NotImplementedError()

    def run(self, N=DEFAULT_N):
        for _ in range(N):
            self._create(target=super(PoolServer, self).run)
        super(PoolServer, self).run()

class ThreadPoolServer(PoolServer):

    def _create(self, target):
        t = threading.Thread(target=target)
        t.daemon = True
        t.start()

class ProcessPoolServer(PoolServer, ProcessServer):

    def _create(self, target):
        self._cond.acquire()
        pid = os.fork()
        if pid == 0:
            target()
            os._exit(0)
        self._pids.append(pid)
        self._cond.release()

SERVERS = [
    SimpleServer,
    EventServer,
    ThreadServer,
    ThreadPoolServer,
    ProcessPoolServer,
]
