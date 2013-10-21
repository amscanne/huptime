#
# Copyright 2013 Adin Scannell <adin@scannell.ca>, all rights reserved.
#
# This file is part of Huptime.
#
# Huptime is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Huptime is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Huptime.  If not, see <http://www.gnu.org/licenses/>.
#
"""
Servers.

This file contains various server implementations,
which can be created by different tests.
"""

import os
import sys
import socket
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

    def bind(self, host=None, port=None):
        if host is None:
            host = DEFAULT_HOST
        if port is None:
            port = DEFAULT_PORT
        sys.stderr.write("%s: bind()\n" % self)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.bind((host, port))

    def close(self):
        sys.stderr.write("%s: close()\n" % self)
        self._sock.close()

    def listen(self, backlog=None):
        if backlog is None:
            backlog = DEFAULT_BACKLOG
        sys.stderr.write("%s: listen()\n" % self)
        self._sock.listen(backlog)

    def accept(self):
        sys.stderr.write("%s: accept()\n" % self)
        client, _ = self._sock.accept()
        return client

    def getpid(self):
        sys.stderr.write("%s: getpid()\n" % self)
        return os.getpid()

    def handle(self, client):
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
            # Occassionally huptime will send back
            # fake clients during a restart to get
            # around race races. We handle this as
            # a serve would handle a bad client.
            client.close()
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
            client.close()

        return rval

    def run(self):
        raise NotImplementedError()

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

class ProcessServer(Server):

    def __init__(self, *args, **kwargs):
        super(ProcessServer, self).__init__(*args, **kwargs)

    def run(self):
        while True:
            client = self.accept()
            pid = os.fork()
            if pid == 0:
                while self.handle(client):
                    # Continue until finished.
                    pass
                os._exit(0)
            else:
                client.close()
                t = threading.Thread(target=lambda: os.waitpid(pid, 0))
                t.daemon = True
                t.start()

class PoolServer(SimpleServer):

    def _create(self, target):
        raise NotImplementedError()

    def run(self, N=None):
        if N is None:
            N = DEFAULT_N
        for _ in range(N):
            self._create(target=super(PoolServer, self).run)
        super(PoolServer, self).run()

class ThreadPoolServer(PoolServer):

    def _create(self, target):
        t = threading.Thread(target=target)
        t.daemon = True
        t.start()

class ProcessPoolServer(PoolServer):

    def _create(self, target):
        pid = os.fork()
        if pid == 0:
            target()
            os._exit(0)
        else:
            t = threading.Thread(target=lambda: os.waitpid(pid, 0))
            t.daemon = True
            t.start()

SERVERS = [
    SimpleServer,
    EventServer,
    ThreadServer,
    ProcessServer,
    ThreadPoolServer,
    ProcessPoolServer,
]
