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
