/*
 * fdtable.c
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
