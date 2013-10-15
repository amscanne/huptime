from pytest import fixture

from servers import SERVERS
from modes import MODES
from handlers import HANDLERS

@fixture(params=SERVERS)
def server(request):
    """ A server object. """
    return request.param

@fixture(params=MODES)
def mode(request):
    """ A mode object. """
    return request.param

@fixture(params=HANDLERS)
def handler(request):
    """ A handler object. """
    return request.param
