/*
 * fdtable.h
 */

#ifndef _FDTABLE_H_
#define _FDTABLE_H_

#include "fdinfo.h"

/* Lookup the given FD. */
fdinfo_t* fd_lookup(int fd);

/* Save the given entry. */
void fd_save(int fd, fdinfo_t* info);

/* Delete the given entry. */
void fd_delete(int fd);

/* Get the maximum possible FD. */
int fd_max(void);

/* Get the maximum tracked FD. */
int fd_limit(void);

#endif
