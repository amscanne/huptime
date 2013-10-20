/*
 * funcs.h
 */

#ifndef _FUNCS_H_
#define _FUNCS_H_

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

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
} funcs_t;

#endif
