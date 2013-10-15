"""
Servers.

This file contains various server implementations,
which can be created by different tests.
"""

import os
import socket
import signal
import threading

DEFAULT_HOST = ""
DEFAULT_PORT = 7869
DEFAULT_BACKLOG = 1
DEFAULT_N = 8

class BasicServer(object):

    """
    A generic server model.

    This server exposes a simple run() method, which
    will bind() and listen() to a socket, then accept()
    and and serve clients individually.
    """

    def __init__(self, handler):
        self._sock = socket.socket()
        self._handler = handler

    def bind(self, host=DEFAULT_HOST, port=DEFAULT_PORT):
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.bind((host, port))

    def listen(self, backlog=DEFAULT_BACKLOG):
        self._sock.listen(backlog)

    def close(self):
        self._sock.close()

    def run(self):
        while True:
            client = self._sock.accept()
            self._handler.on_connect(client)
            while not self._handler.on_data(client):
                # Continue until finished.
                pass

class EventServer(BasicServer):
   
    def run(self):
        super(EventServer, self).run()
        self._fdmap = {self._sock.fileno(): self._sock}
        while True:
            rfds, wfds, efds = select.select(self._fdmap.keys(), [], [])
            for fd in rfds:
                sock = self._fdmap.get(fd)
                if sock == self._sock:
                    # Accept the client.
                    client = self._sock.accept()
                    self._handler.on_connect(client)
                    self._fdmap[client.fileno()] = client
                else:
                    # Process the request.
                    if self._handler.on_data(client):
                        del self._fdmap[sock.fileno()]

class ThreadServer(BasicServer):

    def run(self):
        super(ThreadServer, self).run()
        while True:
            client = self._sock.accept()
            # Fire a thread to handle it.
            def _do():
                self._handler.on_connect(client)
                while not self._handler.on_data(client):
                    pass
            t = threading.Thread(target=_do)
            t.start()

class ProcessServer(BasicServer):

    def run(self):
        super(ProcessServer, self).run()
        while True:
            client = self._sock.accept()
            # Create a child to handle it.
            pid = os.fork()
            if pid == 0:
                self._handler.on_connect(client)
                while not self._handler.on_data(client):
                    pass
                client.close()
                os._exit(0)
            else:
                client.close()

class PoolServer(BasicServer):

    def _create(self, target):
        raise NotImplementedError()

    def _kill(self, pid):
        raise NotImplementedError()

    def run(self, N=DEFAULT_N):
        super_run = super(PoolServer, self).run
        self._pids = []
        for _ in range(N-1):
            self._pids.append(self._create(target=super_run))
        super_run()

    def close(self):
        super(PoolServer, self).close()
        for pid in self._pids:
            self._kill(pid)

class ThreadPoolServer(PoolServer):

    def _create(self, target):
        t = threading.Thread(target=target)
        t.daemon = True
        t.start()
        return t

    def _kill(self, t):
        pass

class ProcessPoolServer(PoolServer):

    def _create(self, target):
        pid = os.fork()
        if pid == 0:
            target()
            os._exit(0)

    def _kill(self, pid):
        os.kill(pid, signal.SIGTERM)
        os.waitpid(pid, 0)

SERVERS = [
    BasicServer,
    EventServer,
    ThreadServer,
    ProcessServer,
    ThreadPoolServer,
    ProcessPoolServer,
]
