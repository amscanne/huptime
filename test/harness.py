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
import traceback
import re

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
        self._proxy._call("exit")
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

    def _restart(self, pid):
        # Send SIGHUP.
        os.kill(pid, signal.SIGHUP)

        # Wait for the SIGHUP to be processed.
        while True:
            # If this file doesn't exist, then
            # the process must have disappeared.
            # That's fine (it's a restart). But
            # if it still exists, we can confirm
            # that the signal is finished by looking
            # at the mask of the process.
            try:
                finished = False
                data = open("/proc/%d/status" % pid, 'r').read()
                for line in data.split("\n"):
                    m = re.match("SigBlk:\s*([0-9a-f]+)", line)
                    if m:
                        # SIGHUP happens to be 1. So if
                        # this is not blocked, the number
                        # will be even. Once the signal is
                        # not blocked, we know it's done.
                        if int(m.group(1), 16) % 2 == 0:
                            return
            except:
                traceback.print_exc()
                return

    def restart(self, cookie=None):
        # Connect clients.
        old_clients = self.clients()

        # Reset the cookie.
        old_cookie = self._cookie
        self._set_cookie(cookie)

        # Hook to fetch current pid. 
        def getpid():
            return self._proxy.getpid()
        orig_pid = getpid()

        # Grab the current pid, and hit
        # the server with a restart signal.
        sys.stderr.write("harness: restart\n")
        pids = self._proxy.restart_pids()
        for pid in pids:
            self._restart(pid)

        # Whenever it's ready, restart the server.
        start_thread = threading.Thread(target=proxy_starter(self._proxy))
        start_thread.daemon = True
        start_thread.start()

        # Call into the mode to validate.
        self._mode.restart(orig_pid, getpid, start_thread)

        # Connect new clients.
        new_clients = self.clients()

        # Check behavior according to the mode.
        # NOTE: The expected behaviour here is that
        # the mode will drop the all clients in order
        # to assert that things are fully working.
        self._mode.check(
                orig_pid, getpid, start_thread,
                old_clients, new_clients,
                old_cookie, self._cookie)
