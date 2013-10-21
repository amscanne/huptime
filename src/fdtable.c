/*
 * fdtable.c
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

#include "fdtable.h"
#include "fdinfo.h"

#include <sys/time.h>
#include <sys/resource.h>

static fdinfo_t **fd_table = NULL;
static int fd_size = 0;

int
fd_limit(void)
{
    return fd_size;
}

int
fd_max(void)
{
    struct rlimit rlim;
    getrlimit(RLIMIT_NOFILE, &rlim);
    return rlim.rlim_max;
}

static inline void
table_ensure(int index)
{
    int orig_size = fd_size;
    if( index < fd_size )
    {
        return;
    }

    if( fd_size == 0 )
    {
        fd_size = 1;
    }
    while( index >= fd_size )
    {
        fd_size *= 2;
    }

    /* Reallocate the table. */
    fd_table = realloc(fd_table, sizeof(fdinfo_t*) * fd_size);

    /* Clear the new entries. */
    memset(&fd_table[orig_size], 0, sizeof(fdinfo_t*) * (fd_size-orig_size));
}

fdinfo_t*
fd_lookup(int fd)
{
    if( fd >= fd_size )
    {
        return NULL;
    }

    return fd_table[fd];
}

void
fd_save(int fd, fdinfo_t *info)
{
    table_ensure(fd);
    fd_table[fd] = info;
}

void
fd_delete(int fd)
{
    if( fd >= fd_size )
    {
        return;
    }

    fd_table[fd] = NULL;
}
