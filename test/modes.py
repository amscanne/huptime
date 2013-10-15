"""
Modes.

These are various modes of operation.
"""

import sys
import os
import time

class Mode(object):

    # Before each RPC call, we check
    # for something in the mode.
    # This is expected to do something,
    # and the validation checks should
    # ensure that the system is still
    # in the expected state.
    def pre(self, name, server):
        if hasattr(self, 'pre_%s' % name):
            getattr(self, 'pre_%s' % name)(server)

    def post(self, name, server):
        if hasattr(self, 'post_%s' % name):
            getattr(self, 'post_%s' % name)(server)

    def cmdline(self, *args):
        cmd = [
            os.path.abspath(
                os.path.join(
                    os.path.dirname(__file__),
                    "..",
                    "bin",
                    "huptime")),
            "--debug",
        ]
        cmd.extend(args)
        return cmd

    def check(self,
            start_thread,
            old_clients, new_clients,
            old_cookie, new_cookie):
        raise NotImplementedError()

    def restart(self, start_thread):
        raise NotImplementedError()

class Fork(Mode):

    def cmdline(self):
        return super(Fork, self).cmdline("--fork")

    def restart(self, start_thread):
        # Should come up immediately.
        sys.stderr.write("mode: waiting for startup...\n")
        start_thread.join()

    def check(self,
            start_thread,
            old_clients, new_clients,
            old_cookie, new_cookie):
        # All the new clients should be responsive.
        sys.stderr.write("mode: checking new clients...\n")
        new_clients.verify([new_cookie])

        # Drop all the old clients.
        old_clients.verify([old_cookie, new_cookie])

class Exec(Mode):

    def cmdline(self):
        return super(Exec, self).cmdline("--exec")

    def restart(self, start_thread):
        # Ensure it's not started yet.
        assert start_thread.isAlive()

    def check(self,
            start_thread,
            old_clients, new_clients,
            old_cookie, new_cookie):
        # All old clients should keep working.
        sys.stderr.write("mode: checking old clients...\n")
        old_clients.verify([old_cookie, new_cookie])

        # Wait for startup (blocking).
        sys.stderr.write("mode: waiting for startup...\n")
        start_thread.join()

        # All the new clients should now be responsive.
        sys.stderr.write("mode: checking new clients...\n")
        new_clients.verify([new_cookie])

MODES = [
    Fork,
    Exec,
]
