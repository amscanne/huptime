"""
A basic test driver.

The harness will run through the basic workflow
and assert that all is well at each step, based
on the behavior asserted by all the different pieces.

This should cover nearly all the code, with the
exception of code edge cases. Separate tests will
exist for this edge cases.
"""

import threading

import harness

def test_inactive(mode, server, handler):
    h = harness.Harness(mode, server, handler)
    try:
        h.proxy.bind()
        h.proxy.listen()
        h.start()
        h.start_client(data=0)
        h.finish_client(data=0)
        h.restart()
        h.start_client(data=1)
        h.finish_client(data=1)
    finally:
        h.stop()

def test_active(mode, server, handler):
    h = harness.Harness(mode, server, handler)
    try:
        h.proxy.bind()
        h.proxy.listen()
        h.start()
        h.start_client(data=0)
        h.restart()
        h.start_client(data=1)
        h.finish_client(data=0)
        h.finish_client(data=1)
    finally:
        h.stop()
