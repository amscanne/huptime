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
        sys.stderr.write("client: connect()\n")
        self._sock.connect((host, port))
        sys.stderr.write("client: connected!\n")

    def cookie(self):
        sys.stderr.write("client: cookie (start)\n")
        self._sock.send("cookie")
        server_cookie = self._sock.recv(1024)
        sys.stderr.write("client: cookie (done)\n")
        return server_cookie

    def ping(self):
        sys.stderr.write("client: ping (start)\n")
        self._sock.send("ping")
        assert self._sock.recv(1024) == "pong"
        sys.stderr.write("client: ping (done)\n")

    def drop(self):
        sys.stderr.write("client: drop (start)\n")
        self._sock.send("drop")
        assert self._sock.recv(1024) == "okay"
        sys.stderr.write("client: drop (done)\n")
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

    def __init__(self, n=DEFAULT_CLIENTS, host=None, port=None):
        super(Clients, self).__init__()
        self._clients = map(lambda x: ClientThread(host=host, port=port), range(n))

    def verify(self, valid_cookies):
        for c in self._clients:
            c.verify(valid_cookies)
