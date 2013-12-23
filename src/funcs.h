/*
 * funcs.h
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

#ifndef HUPTIME_FUNCS_H
#define HUPTIME_FUNCS_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>

/* Typedefs for libc functions that we override. */
typedef int (*bind_t)(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
typedef int (*accept_t)(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
typedef int (*accept4_t)(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags);
typedef int (*listen_t)(int sockfd, int backlog);
typedef int (*close_t)(int fd);
typedef pid_t (*fork_t)(void);
typedef int (*dup_t)(int fd);
typedef int (*dup2_t)(int fd, int fd2);
typedef int (*dup3_t)(int fd, int fd2, int flags);
typedef void (*exit_t)(int status);
typedef pid_t (*wait_t)(void *status);
typedef pid_t (*waitpid_t)(pid_t pid, int *status, int options); 

/* A structure containing all functions. */
typedef struct
{
    bind_t bind;
    listen_t listen;
    accept_t accept;
    accept4_t accept4;
    close_t close;
    fork_t fork;
    dup_t dup;
    dup2_t dup2;
    dup3_t dup3;
    exit_t exit;
    wait_t wait;
    waitpid_t waitpid;
} funcs_t;

#endif
