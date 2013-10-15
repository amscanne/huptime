"""
A basic test driver.

The harness will run through the basic workflow
and assert that all is well at each step, based
on the behavior asserted by all the different pieces.

This should cover nearly all the code, with the
exception of code edge cases. Separate tests will
exist for this edge cases.
"""

import sys
import threading
import pytest

import harness
import servers
import modes

@pytest.fixture(params=map(lambda x: x.__name__, servers.SERVERS))
def server(request):
    """ A server object. """
    return getattr(servers, request.param)

@pytest.fixture(params=map(lambda x: x.__name__, modes.MODES))
def mode(request):
    """ A mode object. """
    return getattr(modes, request.param)

def test_thrice(mode, server):
    h = harness.Harness(mode, server)
    try:
        h.restart()
        h.restart()
        h.restart()
    finally:
        h.stop()
