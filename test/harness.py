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

def proxy_starter(proxy, host=None, port=None, backlog=None):
    def fn():
        proxy._wait()
        proxy.bind(host=host, port=port)
        proxy.listen(backlog=backlog)
        proxy._call("run")
    return fn

class Harness(object):

    def __init__(self, mode_class, server_class, cookie=None, **kwargs):

        super(Harness, self).__init__()
        self._cookie_file = tempfile.NamedTemporaryFile()
        self._set_cookie(cookie)
        self._mode = mode_class()
        self._kwargs = kwargs
        self._proxy = proxy.ProxyClient(
            self._mode,
            server_class,
            self._cookie_file.name)

        # Run the server normally.
        proxy_starter(self._proxy, **self._kwargs)()

    def __getattr__(self, attr):
        return getattr(self._proxy, attr)

    def _set_cookie(self, cookie=None):
        if cookie is None:
            cookie = str(uuid.uuid4())
        self._cookie = cookie
        self._cookie_file.truncate(0)
        self._cookie_file.seek(0, 0)
        self._cookie_file.write(self._cookie)
        self._cookie_file.flush()

    def clients(self, **kwargs):
        return client.Clients(
            host=self._kwargs.get("host"),
            port=self._kwargs.get("port"),
            **kwargs)

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
        self._proxy.restart()

        # Whenever it's ready, restart the server.
        start_thread = threading.Thread(
            target=proxy_starter(self._proxy, **self._kwargs))
        start_thread.daemon = True
        start_thread.start()

        # Call into the mode to validate.
        self._mode.check_restart(orig_pid, getpid, start_thread)

        # Connect new clients.
        new_clients = self.clients()

        # Check behavior according to the mode.
        # NOTE: The expected behaviour here is that
        # the mode will drop the all clients in order
        # to assert that things are fully working.
        self._mode.check_clients(
                orig_pid, getpid, start_thread,
                old_clients, new_clients,
                old_cookie, self._cookie)
