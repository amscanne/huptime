"""
Modes.

These are various modes of operation.
"""

import os

class Mode(object):

    def cmdline(self, *args):
        cmd = [
            os.path.abspath(
                os.path.join(
                    os.path.dirname(__file__),
                    "..",
                    "bin",
                    "huptime")),
        ]
        cmd.extend(args)
        return cmd

    def check_restart(self, handler):
        raise NotImplementedError()

class Fork(Mode):

    def cmdline(self):
        return super(Fork, self).cmdline("--fork")

    def check_restart(self, handler):
        # While we have outstanding processes, we
        # should be able to see the old pid and a
        # new pid simultaneously. We don't track 
        # this directly however, so we can just see
        # that the behavior is as we expect.
        handler.assert_not_blocking()
        handler.assert_data(data)
        handler.close_outstanding()

class Exec(Mode):

    def cmdline(self):
        return super(Exec, self).cmdline("--exec")

    def check_restart(self, handler, data):
        # There's not much that can be done to
        # check a restart at this point. The server
        # will continue to server old clients until
        # all are finished then it will start to 
        # serve the new clients.
        if handler.outstanding():
            handler.assert_blocking()
            handler.close_outstanding()
        handler.assert_not_blocking()
        handler.assert_data(data)

MODES = [
    Fork,
    Exec,
]
