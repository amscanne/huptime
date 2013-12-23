/*
 * fdtable.h
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

#ifndef HUPTIME_FDTABLE_H
#define HUPTIME_FDTABLE_H

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
