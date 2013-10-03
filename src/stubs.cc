/*
 * stubs.cc
 */

extern "C" {
#include "stubs.h"
#include "impl.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
}

template <typename FUNC_T>
static FUNC_T
get_libc_function(const char* name, FUNC_T def)
{
    char *error;
    FUNC_T result;

    /* Clear last error (if any). */
    dlerror();

    /* Try to get the symbol. */
    result = (FUNC_T)dlsym(RTLD_NEXT, name);
    error = dlerror();
    if( result == NULL || error != NULL )
    {
        fprintf(stderr, "dlsym(RTLD_NEXT, \"%s\") failed: %s", name, error);
        result = def;
    }

    return result;
}

static int initialized = 0;

static void __attribute__((constructor))
setup(void)
{
    #define likely(x) __builtin_expect (!!(x), 1)
    if( likely(initialized) )
        return;

    initialized = 1;

    #define GET_LIBC_FUNCTION(_name) \
    libc._name = get_libc_function<_name ## _t>(# _name, &_name)

    GET_LIBC_FUNCTION(bind);
    GET_LIBC_FUNCTION(listen);
    GET_LIBC_FUNCTION(accept);
    GET_LIBC_FUNCTION(accept4);
    GET_LIBC_FUNCTION(close);
    GET_LIBC_FUNCTION(fork);
    GET_LIBC_FUNCTION(dup);
    GET_LIBC_FUNCTION(dup2);
    GET_LIBC_FUNCTION(dup3);
    #undef GET_LIBC_FUNCTION

    impl_init();
}

extern "C"
{

static int
stub_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    setup();
    return impl.bind(sockfd, addr, addrlen);
}

static int
stub_listen(int sockfd, int backlog)
{
    setup();
    return impl.listen(sockfd, backlog);
}

static int
stub_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    setup();
    return impl.accept(sockfd, addr, addrlen);
}

static int
stub_accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
    setup();
    return impl.accept4(sockfd, addr, addrlen, flags);
}

static int
stub_close(int fd)
{
    setup();
    return impl.close(fd);
}

static pid_t
stub_fork()
{
    setup();
    return impl.fork();
}

static int
stub_dup(int fd)
{
    setup();
    return impl.dup(fd);
}

static int
stub_dup2(int fd, int fd2)
{
    setup();
    return impl.dup2(fd, fd2);
}

static int
stub_dup3(int fd, int fd2, int flags)
{
    setup();
    return impl.dup3(fd, fd2, flags);
}

/* Exports name as aliasname in .dynsym. */
#define PUBLIC_ALIAS(name, aliasname)                                       \
    typeof(name) aliasname __attribute__ ((alias (#name)))                  \
                           __attribute__ ((visibility ("default")));

/* Exports stub_ ##name as name@version. */
#define SYMBOL_VERSION(name, version, version_ident)                      \
    PUBLIC_ALIAS(stub_ ## name, stub_ ## name ## _ ## version_ident);     \
    asm(".symver stub_" #name "_" #version_ident ", " #name "@" version);

/* Exports stub_ ##name as name@@ (i.e., the unversioned symbol for name). */
#define GLIBC_DEFAULT(name) \
    SYMBOL_VERSION(name, "@", default_)

/* Exports stub_ ##name as name@@GLIBC_MAJOR.MINOR.PATCH. */
#define GLIBC_VERSION(name, major, minor, patch)                   \
    SYMBOL_VERSION(name, "GLIBC_" # major "." # minor "." # patch, \
                   glibc_ ## major ## minor ## patch)              \

GLIBC_DEFAULT(bind)
GLIBC_VERSION(bind, 2, 2, 5)
GLIBC_DEFAULT(listen)
GLIBC_VERSION(listen, 2, 2, 5)
GLIBC_DEFAULT(accept)
GLIBC_VERSION(accept, 2, 2, 5)
GLIBC_DEFAULT(accept4)
GLIBC_VERSION(accept4, 2, 2, 5)
GLIBC_DEFAULT(close)
GLIBC_VERSION(close, 2, 2, 5)
GLIBC_DEFAULT(fork)
GLIBC_VERSION(fork, 2, 2, 5)
GLIBC_DEFAULT(dup)
GLIBC_VERSION(dup, 2, 2, 5)
GLIBC_DEFAULT(dup2)
GLIBC_VERSION(dup2, 2, 2, 5)
GLIBC_DEFAULT(dup3)
GLIBC_VERSION(dup3, 2, 2, 5)

}
