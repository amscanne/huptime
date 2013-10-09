/*
 * fdinfo.c
 */

#include "fdinfo.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

/* Total active bound FDs. */
int total_bound = 0;

/* Total active tracked FDs. */
int total_tracked = 0;

/* Total saved FDs. */
int total_saved = 0;

#define exactly(fn, fd, buf, bytes)     \
do {                                    \
    for( int _n = 0; _n != bytes; )     \
    {                                   \
        int _t = fn(fd, buf, bytes-_n); \
        if( _t < 0 &&                   \
            (errno == EINTR ||          \
             errno == EAGAIN) )         \
        {                               \
            continue;                   \
        }                               \
        if( _t <= 0 )                   \
        {                               \
            return -1;                  \
        }                               \
        _n += _t;                       \
    }                                   \
} while(0)

int
info_decode(int pipe, int *fd, fdinfo_t **info)
{
    fdtype_t type;

    /* Decode the FD. */
    exactly(read, pipe, fd, sizeof(int));

    /* Decode the type. */
    exactly(read, pipe, &type, sizeof(fdtype_t));

    /* Allocate. */
    *info = alloc_info(type);

    int listened = 0;

    switch( type )
    {
        case BOUND:
            /* Read whether it was listened or not. */
            exactly(read, pipe, &listened, sizeof(int));
            (*info)->bound.real_listened = listened;
            (*info)->bound.stub_listened = 0;
            (*info)->bound.is_ghost = 1;

            /* Decode the bound address. */
            (*info)->bound.addrlen = sizeof(struct sockaddr);
            if( getsockname(
                    *fd,
                    &(*info)->bound.addr,
                    &(*info)->bound.addrlen) < 0 )
            {
                return -1;
            }
            break;

        case SAVED:
            /* Read the original FD. */
            exactly(read, pipe,
                    &(*info)->saved.fd,
                    sizeof((*info)->saved.fd));
            break;

        case TRACKED:
            /* Should never happen. */
            break;
    }

    return 0;
}

int
info_encode(int pipe, int fd, fdinfo_t* info)
{
    /* Encode the FD. */
    exactly(write, pipe, &fd, sizeof(int));

    /* Encode the type. */
    exactly(write, pipe, &info->type, sizeof(fdtype_t));

    int listened = 0;

    switch( info->type )
    {
        case BOUND:
            listened = info->bound.real_listened;
            /* Write whether it was listened or not. */
            exactly(write, pipe, &listened, sizeof(int));
            break;

        case SAVED:
            /* Write the original FD. */
            exactly(write, pipe,
                    &info->saved.fd,
                    sizeof(info->saved.fd));
            break;

        case TRACKED:
            break;
    }

    return 0;
}
