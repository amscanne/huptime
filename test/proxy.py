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
Proxy.

This proxy is used to drive the server classes
through the huptime binary, but still have them
accessible from the test harness.
"""

import os
import sys
import uuid
import subprocess
import threading
import traceback
import pickle

import modes
import servers

class ProxyServer(object):

    def __init__(self, mode_name, server_name, cookie_file):
        cookie = open(cookie_file, 'r').read()
        self._mode = getattr(modes, mode_name)()
        self._server = getattr(servers, server_name)(cookie)
        self._cond = threading.Condition()

    def run(self):
        # Open our pipes.
        in_pipe = os.fdopen(os.dup(0), 'r')
        out_pipe = os.fdopen(os.dup(1), 'w')
        devnull = open("/dev/null", 'r')
        os.dup2(devnull.fileno(), 0)
        devnull.close()
        os.dup2(2, 1)

        # Dump our startup message.
        robj = {
            "id": None,
            "result": None
        }
        out_pipe.write(pickle.dumps(robj))
        out_pipe.flush()
        sys.stderr.write("proxy %d: started.\n" % os.getpid())

        # Get the call from the other side.
        while True:
            try:
                obj = pickle.load(in_pipe)
                sys.stderr.write("proxy %d: <- %s\n" % (os.getpid(), obj))
            except:
                # We're done!
                break

            def closure(obj, out_pipe):
                def fn():
                    self._process(obj, out_pipe)
                return fn

            t = threading.Thread(target=closure(obj, out_pipe))
            t.start()

    def _process(self, obj, out):
        uniq = obj.get("id")
        try:
            if not "method_name" in obj:
                raise ValueError("no method_name?")
            method_name = obj["method_name"]
            args = obj.get("args")
            kwargs = obj.get("kwargs")
            if method_name:
                method = getattr(self._server, method_name)
                self._mode.pre(method_name, self._server)
                result = method(*args, **kwargs)
                self._mode.post(method_name, self._server)
            else:
                result = None
            robj = {
                "id": uniq,
                "result": result
            }
        except Exception as e:
            traceback.print_exc()
            robj = {
                "id": uniq,
                "exception": e
            }

        self._cond.acquire()
        try:
            sys.stderr.write("proxy %d: -> %s\n" % (os.getpid(), robj))
            out.write(pickle.dumps(robj))
            out.flush()
        finally:
            self._cond.release()

class ProxyClient(object):

    def __init__(self, mode, server_class, cookie_file):
        super(ProxyClient, self).__init__()
        self._mode = mode
        self._server_class = server_class
        self._cond = threading.Condition()
        self._results = {}
        self._cookie_file = cookie_file
        self._cmdline = [
            "python",
            __file__,
            mode.__class__.__name__,
            server_class.__name__,
            self._cookie_file,
        ]

        r, w = os.pipe()
        self._out = os.fdopen(w, 'w')
        proc_in = os.fdopen(r, 'r')

        r, w = os.pipe()
        self._in = os.fdopen(r, 'r')
        proc_out = os.fdopen(w, 'w')

        self._mode.start(
            self._cmdline,
            stdin=proc_in,
            stdout=proc_out,
            close_fds=True)

        proc_in.close()
        proc_out.close()

        # Start the processing thread.
        t = threading.Thread(target=self._run)
        t.daemon = True
        t.start()

    def _call(self, method_name=None, args=None, kwargs=None):
        if args is None:
            args = []
        if kwargs is None:
            kwargs = {}

        # Send the call to the other side.
        uniq = str(uuid.uuid4())
        obj = {
            "id": uniq,
            "method_name": method_name,
            "args": args,
            "kwargs": kwargs
        }
        sys.stderr.write("proxy client: -> %s\n" % obj)
        self._out.write(pickle.dumps(obj))
        self._out.flush()

        return uniq

    def _wait(self, uniq=None, method_name=None):
        # Wait for a result to appear.
        self._cond.acquire()
        try:
            while True:
                if uniq in self._results:
                    res = self._results[uniq]
                    del self._results[uniq]
                    if "exception" in res:
                        raise res["exception"]
                    elif "result" in res:
                        return res["result"]
                    else:
                        raise ValueError("no result?")
                sys.stderr.write("proxy client: waiting for %s (%s)...\n" %
                    (uniq, method_name))
                self._cond.wait()
        finally:
            self._cond.release()

    def _run(self):
        # Get the return from the other side.
        while True:
            try:
                obj = pickle.load(self._in)
                sys.stderr.write("proxy client: <- %s\n" % obj)
            except:
                # We're done!
                break
            self._cond.acquire()
            try:
                uniq = obj.get("id")
                self._results[uniq] = obj
                self._cond.notifyAll()
            finally:
                self._cond.release()

    def stop(self):
        self._mode.stop(self._cmdline)

    def restart(self):
        self._mode.restart(self._cmdline)

    def __getattr__(self, method_name):
        def _fn(*args, **kwargs):
            uniq = self._call(method_name, args, kwargs)
            return self._wait(uniq, method_name=method_name)
        return _fn

if __name__ == "__main__":
    proxy = ProxyServer(*sys.argv[1:])
    proxy.run()
