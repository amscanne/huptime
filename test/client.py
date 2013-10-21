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
The client.

We implement a simple client that talks to the
simple server protocol implemented by servers.py.
"""

import sys
import socket
import unittest
import threading
import traceback

import servers

DEFAULT_CLIENTS = 10

class Client(object):

    def __init__(self, host=None, port=None):
        super(Client, self).__init__()
        if host is None:
            host = "localhost"
        if port is None:
            port = servers.DEFAULT_PORT
        self._sock = socket.socket()
        self._sock.connect((host, port))

    def cookie(self):
        self._sock.send("cookie")
        server_cookie = self._sock.recv(1024)
        return server_cookie

    def ping(self):
        self._sock.send("ping")
        assert self._sock.recv(1024) == "pong"

    def drop(self):
        self._sock.send("drop")
        assert self._sock.recv(1024) == "okay"
        self._sock.close()

class ClientThread(threading.Thread):

    def __init__(self, **kwargs):
        super(ClientThread, self).__init__()
        self._client = Client(**kwargs)
        self._cookie = None
        self._exception = None
        self.daemon = True
        self.start()

    def run(self):
        try:
            self._client.ping()
            self._cookie = self._client.cookie()
            self._client.drop()
        except Exception as e:
            self._exception = e

    def verify(self, valid_cookies):
        self.join()
        if self._exception:
            raise self._exception
        assert self._cookie in valid_cookies

class Clients(object):

    def __init__(self, N=None, host=None, port=None):
        super(Clients, self).__init__()
        if N is None:
            N = DEFAULT_CLIENTS
        self._clients = map(lambda x: ClientThread(host=host, port=port), range(N))

    def verify(self, valid_cookies):
        for c in self._clients:
            c.verify(valid_cookies)
