/*
 * fdinfo.c
 */

#include "fdinfo.h"

#include <stdio.h>
#include <string.h>

/* Total active bound FDs. */
int total_bound = 0;

/* Total active tracked FDs. */
int total_tracked = 0;

/* Total saved FDs. */
int total_saved = 0;

fdinfo_t*
info_decode(int fd, const char* data)
{
    /* Check for a valid encoding (per below). */
    if( data == NULL || strlen(data) < 2 )
    {
        return NULL;
    }

    /* We only accept BOUND sockets currently. */
    if( data[0] == 'B' )
    {
        fdinfo_t *info = alloc_info(BOUND);
        info->bound.addrlen = sizeof(struct sockaddr);
        if( getsockname(fd, &info->bound.addr, &info->bound.addrlen) < 0 )
        {
            dec_ref(info);
            return NULL;
        }

        if( data[1] == '1' )
        {
            info->bound.real_listened = 1;
        }

        /* Stub has never listened yet. */
        info->bound.stub_listened = 0;

        /* Always a ghost. */
        info->bound.is_ghost = 1;

        return info;
    }
    else if( data[0] == 'S' )
    {
        fdinfo_t *info = alloc_info(SAVED);

        /* Decode the original FD. */
        info->saved.fd = strtol(&data[1], NULL, 16);

        return info;
    }

    return NULL;
}

const char*
info_encode(int fd, fdinfo_t* info)
{
    char buf[10];

    if( info != NULL && info->type == BOUND )
    {
        buf[0] = 'B';
        buf[1] = info->bound.real_listened ? '1' : '0';
        buf[2] = '\0';
        return strdup(buf);
    }
    else if( info != NULL && info->type == SAVED )
    {
        snprintf(&buf[0], 9, "S%x", info->saved.fd);
        return strdup(buf);
    }

    return NULL;
}
