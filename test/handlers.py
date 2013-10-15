"""
Handlers.

In practice, we only have a single handler,
which is the Ping handler. It allows us to
stuff unique data which is dumped immediately
on connection to the server, and then simply
returns data on a ping/pong basis.
"""

import uuid
import tempfile

class Handler(object):

    def args(self):
        return []

    def on_connect(self, client):
        pass

    def on_data(self, client):
        return True

class Ping(Handler):

    def __init__(self, source_file=None):
        if not source_file:
            self._source_file = tempfile.NamedTemporaryFile()
            self._msg = str(uuid.uuid4())
        else:
            self._source_file = source_file
            self._msg = open(self._source_file, 'r').read().strip()

    def args(self):
        return [self._source_file.name]

    def on_connect(self, client):
        pass

    def on_data(self, client):
        helo = client.readline()
        client.write(self._msg)
        client.close()
        return True

HANDLERS = [
    Ping,
]
