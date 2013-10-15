"""
Servers.

This file contains various server implementations,
which can be created by different tests.
"""

import os
import sys
import socket
import signal
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
        self._cond.acquire()
        try:
            sys.stderr.write("%s: accept()\n" % self)
            client, _ = self._sock.accept()
            self._clients += 1
            sys.stderr.write("%s: accepted!\n" % self)
        finally:
            self._cond.release()

        return client

    def getpids(self):
        sys.stderr.write("%s: getpids()\n" % self)
        return [os.getpid()]

    def close(self):
        sys.stderr.write("%s: close()\n" % self)
        self._sock.close()
        os._exit(0)

    def _handle(self, client):
        # Wait for data on the socket.
        while True:
            try:
                sys.stderr.write("%s: select()\n" % self)
                rfds, _, _ = select.select([client.fileno()], [], [])
                if client.fileno() in rfds:
                    break
            except select.error as e:
                if e[0] != errno.EINTR:
                    raise

        # This implements a very simple protocol that
        # allows us to test for a code "version" (by the
        # cookie at startup) and liveness (via ping).
        # The corresponding client code is available in
        # client.py.
        rval = False
        sys.stderr.write("%s: recv()\n" % self)
        command = client.recv(1024)
        if not command:
            return True

        sys.stderr.write("%s: %s (start)\n" % (self, command))
        if command == "cookie":
            sys.stderr.write("%s: cookie == %s\n" % (self, self._cookie))
            client.send(self._cookie)
            rval = True
        elif command == "ping":
            client.send("pong")
            rval = True
        elif command == "drop":
            client.send("okay")
            self.drop(client)
        else:
            self.drop(client)
        sys.stderr.write("%s: %s (done)\n" % (self, command))
        return rval

    def drop(self, client):
        self._cond.acquire()
        try:
            client.close()
            self._clients -= 1
            self._cond.notifyAll()
        finally:
            self._cond.release()

    def wait(self):
        self._cond.acquire()
        try:
            while self._clients > 0:
                self._cond.wait()
        finally:
            self._cond.release()

    def handle(self, client):
        try:
            sys.stderr.write("%s: handle() (%d active)\n" % (self, self._clients))
            return self._handle(client)
        except socket.error as e:
            traceback.print_exc()
            if e.args[0] in (errno.EAGAIN, errno.EINTR):
                return True
            else:
                self.drop(client)
                return False

    def run(self):
        while True:
            client = self.accept()
            while self.handle(client):
                # Continue until finished.
                pass

    def __str__(self):
        return "%s %d" % (self.__class__.__name__, os.getpid())

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

class ThreadServer(Server):

    def run(self):
        while True:
            client = self.accept()
            # Fire a thread to handle it.
            def closure(c):
                def fn():
                    while self.handle(c):
                        pass
                sys.stderr.write("%s: thread_exit()\n" % self)
                return fn
            t = threading.Thread(target=closure(client))
            t.daemon = True
            t.start()

class ProcessServer(Server):

    def run(self):
        while True:
            client = self.accept()
            # Create a child to handle it.
            pid = os.fork()
            if pid == 0:
                while self.handle(client):
                    pass
                os._exit(0)
            else:
                self.drop(client)
                os.waitpid(pid, 0)

class PoolServer(Server):

    _pids = []

    def _create(self, target):
        raise NotImplementedError()

    def _kill(self, pid):
        raise NotImplementedError()

    def _wait(self, pid):
        raise NotImplementedError()

    def run(self, N=DEFAULT_N):
        self._pids = []
        for _ in range(N):
            self._pids.append(
                self._create(target=super(PoolServer, self).run))
        for pid in self._pids:
            self._wait(pid)

    def close(self):
        sys.stderr.write("%s: close()\n" % self)
        for pid in self._pids:
            self._kill(pid)
        super(PoolServer, self).close()

class ThreadPoolServer(PoolServer):

    def _create(self, target):
        t = threading.Thread(target=target)
        t.daemon = True
        t.start()
        return t

    def _kill(self, t):
        pass

    def _wait(self, t):
        t.join()

class ProcessPoolServer(PoolServer):

    def _create(self, target):
        pid = os.fork()
        if pid == 0:
            target()
            os._exit(0)
        return pid

    def getpids(self):
        pids = super(ProcessPoolServer, self).getpids()
        pids.extend(self._pids)
        return pids

    def _kill(self, pid):
        os.kill(pid, signal.SIGTERM)

    def _wait(self, pid):
        os.waitpid(pid, 0)

SERVERS = [
    Server,
    EventServer,
    # ThreadServer,
    ProcessServer,
    # ThreadPoolServer,
    # ProcessPoolServer,
]
