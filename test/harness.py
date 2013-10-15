"""
Harness.

A generic test harness.
"""

import threading

import proxy

class Harness(object):

    def __init__(self, mode_class, server_class, handler_class, variant_classes=None):
        if variant_classes is None:
            variant_classes = []
        self._mode = mode_class()
        self._handler = handler_class()
        self._proxy = proxy.ProxyClient(
            self._mode.cmdline(),
            server_class.__name__,
            handler_class.__name__,
            map(lambda x: x.__name__, variant_classes),
            *self._handler.args())
        self._thread = None
        self._clients = {}
        self._done = False

    def __del__(self):
        if self._thread:
            self.stop()

    @property
    def proxy(self):
        return self._proxy

    def stop(self):
        if self._done:
            raise Exception("already stopped!")

        self._done = True
        try:
            self._proxy._call('close')
        except:
            # It's okay, the server may have failed.
            # (though we should have caught this at
            # a different point and failed the test).
            pass
        self._proxy._stop()

        if self._thread:
            # Ignore the return of the thread.
            self._thread = None

    def start(self):
        if self._done:
            raise Exception("already done!")
        if self._thread:
            raise Exception("already started!")

        def _run():
            try:
                self._proxy.run()
            except IOError:
                # This is expected.
                pass

        self._thread = threading.Thread(target=_run)
        self._thread.daemon = True
        self._thread.start()

    def start_client(self, data=0):
        print "start_client", data

    def restart(self):
        print "restart"

    def finish_client(self, data=0):
        print "finish_client", data
