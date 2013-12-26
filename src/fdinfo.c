/*
 * fdinfo.c
 *
 * Copyright 2013 Adin Scannell <adin@scannell.ca>, all rights reserved.
 *
 * This file is part of Huptime.
 *
 * Huptime is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Huptime is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Huptime.  If not, see <http://www.gnu.org/licenses/>.
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

/* Total initial FDs. */
int total_initial = 0;

/* Total dummy FDs. */
int total_dummy = 0;

/* Total epoll FDs. */
int total_epoll = 0;

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

            /* Read the bound address. */
            exactly(read, pipe, &(*info)->bound.addrlen, sizeof(socklen_t));
            if( (*info)->bound.addrlen > 0 )
            {
                (*info)->bound.addr = malloc((*info)->bound.addrlen);
                exactly(read, pipe, (*info)->bound.addr, (*info)->bound.addrlen);
            }
            break;

        case SAVED:
            /* Read the original FD. */
            exactly(read, pipe,
                    &(*info)->saved.fd,
                    sizeof((*info)->saved.fd));

            /* Read the original offset. */
            exactly(read, pipe,
                    &(*info)->saved.offset,
                    sizeof((*info)->saved.offset));
            break;

        case TRACKED:
        case DUMMY:
        case EPOLL:
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

            /* Write the bound address. */
            exactly(write, pipe, &info->bound.addrlen, sizeof(socklen_t));
            if( info->bound.addrlen > 0 )
            {
                exactly(write, pipe, info->bound.addr, info->bound.addrlen);
            }
            break;

        case SAVED:
            /* Write the original FD. */
            exactly(write, pipe,
                    &info->saved.fd,
                    sizeof(info->saved.fd));

            /* Write the original offset. */
            exactly(write, pipe,
                    &info->saved.offset,
                    sizeof(info->saved.offset));
            break;

        case TRACKED:
        case DUMMY:
        case EPOLL:
            /* Should never happen. */
            break;
    }

    return 0;
}
