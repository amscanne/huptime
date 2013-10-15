"""
Harness.

A generic test harness.
"""

import sys
import unittest
import signal
import os
import time
import uuid
import tempfile
import threading

import proxy
import client

def proxy_starter(proxy):
    def fn():
        proxy._wait()
        proxy.bind()
        proxy.listen()
        proxy._call("run")
    return fn

class Harness(object):

    def __init__(self, mode_class, server_class, cookie=None):

        super(Harness, self).__init__()
        self._cookie_file = tempfile.NamedTemporaryFile()
        self._set_cookie(cookie)
        self._mode = mode_class()
        self._proxy = proxy.ProxyClient(
            self._mode.cmdline(),
            mode_class,
            server_class,
            self._cookie_file.name)

        # Run the server normally.
        proxy_starter(self._proxy)()

    def stop(self):
        self._proxy._call("close")
        self._proxy._join()

    @property
    def proxy(self):
        return self._proxy

    def _set_cookie(self, cookie=None):
        if cookie is None:
            cookie = str(uuid.uuid4())
        self._cookie = cookie
        self._cookie_file.truncate(0)
        self._cookie_file.seek(0, 0)
        self._cookie_file.write(self._cookie)
        self._cookie_file.flush()

    def client(self):
        return client.Client()

    def clients(self):
        return client.Clients()

    def restart(self, cookie=None):
        # Connect clients.
        old_clients = self.clients()

        # Reset the cookie.
        old_cookie = self._cookie
        self._set_cookie(cookie)

        # Grab the current pid, and hit
        # the server with a restart signal.
        sys.stderr.write("harness: restart\n")
        pids = self._proxy.getpids()
        for pid in pids:
            os.kill(pid, signal.SIGHUP)
        for pid in pids:
            self._proxy._call()

        # Whenever it's ready, restart the server.
        start_thread = threading.Thread(target=proxy_starter(self._proxy))
        start_thread.daemon = True
        start_thread.start()

        # Call into the mode to validate.
        self._mode.restart(start_thread)

        # Connect new clients.
        new_clients = self.clients()

        # Check behavior according to the mode.
        # NOTE: The expected behaviour here is that
        # the mode will drop the all clients in order
        # to assert that things are fully working.
        self._mode.check(
                start_thread,
                old_clients, new_clients,
                old_cookie, self._cookie)
