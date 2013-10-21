"""
Modes.

These are various modes of operation.
"""

import sys
import os
import time
import subprocess
import threading

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

    def _run(self, cmdline, reap=True, **kwargs):
        args = self._args()
        cmd = [
            os.path.abspath(
                os.path.join(
                    os.path.dirname(__file__),
                    "..",
                    "bin",
                    "huptime")),
            "--debug"
        ]
        cmd.extend(args)
        cmd.extend(cmdline)
        sys.stderr.write("exec: %s\n" % " ".join(cmd))
        return subprocess.Popen(cmd, **kwargs)

    def _args(self):
        raise NotImplementedError()

    def start(self, cmdline, **kwargs):
        proc = self._run(cmdline, **kwargs)
        t = threading.Thread(target=lambda: proc.wait())
        t.daemon = True
        t.start()

    def stop(self, cmdline):
        proc = self._run(["--stop"] + cmdline)
        proc.wait()

    def restart(self, cmdline):
        proc = self._run(["--restart"] + cmdline)
        proc.wait()

    def status(self, cmdline):
        proc = self._run(
            ["--restart"] + cmdline,
            stdout=subprocess.PIPE)
        proc.wait()
        if proc.returncode != 0:
            return []
        else:
            return map(
                lambda x: x.strip(),
                proc.stdout.readlines())

    def check_clients(self,
            start_thread,
            old_clients, new_clients,
            old_cookie, new_cookie):
        raise NotImplementedError()

    def check_restart(self, start_thread):
        raise NotImplementedError()

    def __str__(self):
        return self.__class__.__name__

class Fork(Mode):

    def _args(self):
        return ["--fork"]

    def check_restart(self, pid, getpid, start_thread):
        # Should come up immediately.
        sys.stderr.write("%s: waiting for startup...\n" % self)
        start_thread.join()

        # Ensure that it's a new pid.
        assert pid != getpid()

    def check_clients(self,
            pid, getpid, start_thread,
            old_clients, new_clients,
            old_cookie, new_cookie):
        # All the new clients should be responsive.
        sys.stderr.write("%s: checking new clients...\n" % self)
        new_clients.verify([new_cookie])

        # Drop all the old clients.
        old_clients.verify([old_cookie, new_cookie])

class Exec(Mode):

    def _args(self):
        return ["--exec"]

    def check_restart(self, pid, getpid, start_thread):
        # Ensure it's not started yet.
        assert start_thread.isAlive()

    def check_clients(self,
            pid, getpid, start_thread,
            old_clients, new_clients,
            old_cookie, new_cookie):

        # All old clients should keep working.
        sys.stderr.write("%s: checking old clients...\n" % self)
        old_clients.verify([old_cookie, new_cookie])

        # Wait for startup (blocking).
        sys.stderr.write("%s: waiting for startup...\n" % self)
        start_thread.join()

        # Ensure it's still the same pid.
        assert pid == getpid()

        # All the new clients should now be responsive.
        sys.stderr.write("%s: checking new clients...\n" % self)
        new_clients.verify([new_cookie])

MODES = [
    Fork,
    Exec,
]
