import itertools

from pytest import fixture

from servers import SERVERS
from modes import MODES
from handlers import HANDLERS
from variants import VARIANTS

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

VARIANT_COMBINATIONS = list(itertools.chain(
    *[itertools.combinations(VARIANTS, ni) for ni in range(len(VARIANTS)+1)]))

@fixture(params=VARIANT_COMBINATIONS)
def variants(request):
    """ A set of variants. """
    return request.param
