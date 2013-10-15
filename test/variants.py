"""
Variants.

These place simple variants on the
behavior of the server. We test all
combinations of these variations.
"""

class Variant(object):

    # Before each RPC call, we check
    # for something in the variant.
    # This is expected to do something,
    # and the validation checks should
    # ensure that the system is still
    # in the expected state.
    def pre(self, name, server, handler):
        if hasattr(self, 'pre_%s' % name):
            getattr(self, 'pre_%s' % name)(server, handler)

    def post(self, name, server, handler):
        if hasattr(self, 'post_%s' % name):
            getattr(self, 'post_%s' % name)(server, handler)

class Pidfile(Variant):

    def post_bind(self, server, handler):
        pass

class LockFile(Variant):

    def post_bind(self, server, handler):
        pass

VARIANTS = [
    Pidfile,
    LockFile
]
