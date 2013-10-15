"""
Proxy.

This proxy is used to drive the server classes
through the huptime binary, but still have them
accessible from the test harness.
"""

import sys
import uuid
import subprocess
import threading
import traceback
import pickle

import servers
import handlers
import variants

class ProxyServer(object):

    def __init__(self, server_name, handler_name, variant_names, *handler_args):
        # Get the real class.
        server_class = getattr(servers, server_name)
        handler_class = getattr(handlers, handler_name)
        variant_names = variant_names.split(",")
        self._variants = map(lambda x: getattr(variants, x)(), [vn for vn in variant_names if vn])
        self._handler = handler_class(*handler_args)
        self._server = server_class(self._handler)

    def run(self):
        # Get the call from the other side.
        while True:
            try:
                obj = pickle.load(sys.stdin)
                sys.stderr.write("server <- %s\n" % obj)
            except:
                # We're done!
                break
            uniq = obj.get("id")
            try:
                if not "method_name" in obj:
                    raise ValueError("no method_name?")
                method_name = obj["method_name"]
                args = obj.get("args")
                kwargs = obj.get("kwargs")
                method = getattr(self._server, method_name)
                for variant in self._variants:
                    variant.pre(method_name, self._server, self._handler)
                result = method(*args, **kwargs)
                for variant in self._variants:
                    variant.post(method_name, self._server, self._handler)
                robj = {
                    "id": uniq,
                    "result": result
                }
            except Exception as e:
                traceback.print_exc(e)
                robj = {
                    "id": uniq,
                    "exception": e
                }
            sys.stderr.write("server -> %s\n" % obj)
            pickle.dump(robj, sys.stdout)
            sys.stdout.flush()

class ProxyClient(object):

    def __init__(self, cmdline, server_name, handler_name, variant_names, *handler_args):
        self._cond = threading.Condition()
        self._results = {}
        cmdline = cmdline[:]
        cmdline.extend([
            "python",
            __file__,
            server_name,
            handler_name,
            ",".join(variant_names)])
        cmdline.extend(handler_args)
        self._proc = subprocess.Popen(
            cmdline,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE)

        self._thread = threading.Thread(target=self._run)
        self._thread.daemon = True
        self._thread.start()

    def _stop(self):
        self._proc.kill()
        self._proc.wait()

    def _call(self, method_name, args=None, kwargs=None):
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
        sys.stderr.write("client -> %s\n" % obj)
        pickle.dump(obj, self._proc.stdin)
        self._proc.stdin.flush()

        return uniq

    def _wait(self, uniq):
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
                self._cond.wait()
        finally:
            self._cond.release()

    def _run(self):
        # Get the return from the other side.
        while True:
            try:
                obj = pickle.load(self._proc.stdout)
                sys.stderr.write("client <- %s\n" % obj)
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

    def __getattr__(self, method_name):
        def _fn(*args, **kwargs):
            uniq = self._call(method_name, args, kwargs)
            return self._wait(uniq)
        _fn.__name__ = method_name
        return _fn

if __name__ == "__main__":
    proxy = ProxyServer(*sys.argv[1:])
    proxy.run()
