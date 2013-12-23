/*
 * impl.c
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

#include "impl.h"
#include "stubs.h"
#include "fdinfo.h"
#include "fdtable.h"
#include "utils.h"

#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <poll.h>

typedef enum 
{
    FORK = 1,
    EXEC = 2,
} exit_strategy_t;

typedef enum
{
    FALSE = 0,
    TRUE = 1,
} bool_t;

/* Copy of execution environment. */
static char **environ_copy = NULL;
static char **args_copy = NULL;
static char *exe_copy = NULL;
static char *cwd_copy = NULL;

/* Whether or not we are currently exiting. */
static bool_t is_exiting = FALSE;

/* Our exit strategy (set on startup). */
static exit_strategy_t exit_strategy = FORK;

/* Files to unlink? */
static char *to_unlink = NULL;

/* Multi mode? */
static bool_t multi_mode = FALSE;

/* Revive mode? */
static bool_t revive_mode = FALSE;

/* Wait mode? */
static bool_t wait_mode = FALSE;

/* Whether or not our HUP handler will exit or restart. */
static pid_t master_pid = (pid_t)-1;

/* Debug hook. */
static bool_t debug_enabled = FALSE;

#define DEBUG(fmt, args...)                                         \
    do {                                                            \
        if( debug_enabled == TRUE )                                 \
        {                                                           \
            pid_t pid = getpid();                                   \
            fprintf(stderr, "huptime %d: " fmt "\n", pid, ## args); \
            fflush(stderr);                                         \
        }                                                           \
    } while(0)

/* Lock (for thread-safe fd tracking). */
static pthread_mutex_t mutex;

#define L()                               \
    do {                                  \
        DEBUG("-wait- %d", __LINE__);     \
        pthread_mutex_lock(&mutex);       \
        DEBUG("-acquired- %d", __LINE__); \
    } while(0)

#define U()                              \
    do {                                 \
        DEBUG("-release- %d", __LINE__); \
        pthread_mutex_unlock(&mutex);    \
    } while(0)

/* Our restart signal pipe. */
static int restart_pipe[2] = { -1, -1 };

/* Our core signal handlers. */
static void* impl_restart_thread(void*);
void
sighandler(int signo)
{
    /* Notify the restart thread.
     * We have to do this in a separate thread, because
     * we have no guarantees about which thread has been
     * interrupted in order to execute this signal handler.
     * Because this could have happened during a critical
     * section (i.e. locks held) we have no choice but to
     * fire the restart asycnhronously so that it too can
     * grab locks appropriately. */
    DEBUG("Restart caught.");

    if( restart_pipe[1] == -1 )
    {
        /* We've already run. */
        return;
    }

    while( 1 )
    {
        char go = 'R';
        int rc = write(restart_pipe[1], &go, 1);
        if( rc == 0 )
        {
            /* Wat? Try again. */
            continue;
        }
        else if( rc == 1 )
        {
            /* Done. */
            libc.close(restart_pipe[1]);
            restart_pipe[1] = -1;
            break;
        }
        else if( rc < 0 && (errno == EAGAIN || errno == EINTR) )
        {
            /* Go again. */
            continue;
        }
        else
        {
            /* Shit. */
            DEBUG("Restart pipe fubared!? Sorry.");
            break;
        }
    }
}

static int
do_dup(int fd)
{
    int rval = -1;
    fdinfo_t *info = NULL;

    DEBUG("do_dup(%d, ...) ...", fd);
    L();
    info = fd_lookup(fd);
    if( info == NULL )
    {
        U();
        rval = libc.dup(fd);
        DEBUG("do_dup(%d) => %d (no info)", fd, rval);
        return rval;
    }

    rval = libc.dup(fd);
    if( rval >= 0 )
    {
        inc_ref(info);
        fd_save(rval, info);
    }

    U();
    DEBUG("do_dup(%d) => %d (with info)", fd, rval);
    return rval;
}

void
impl_exec(void)
{
    DEBUG("Preparing for exec...");

    /* Reset our signal masks.
     * We intentionally mask SIGHUP here so that
     * it can't be called prior to us installing
     * our signal handlers. */
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGUSR2);
    sigprocmask(SIG_BLOCK, &set, NULL);

    /* Encode extra information.
     *
     * This includes information about sockets which
     * are in the BOUND or SAVED state. Note that we
     * can't really do anything with these *now* as
     * there are real threads running rampant -- so
     * we encode things for the exec() and take care 
     * of it post-exec(), where we know we're solo.
     *
     * This information is encoded into a pipe which
     * is passed as an extra environment variable into
     * the next child. Although there is a limit on the
     * amount of data that can be stuffed into a pipe,
     * past Linux 2.6.11 (IIRC) this is 65K. */
    int pipes[2];
    if( pipe(pipes) < 0 )
    {
        DEBUG("Unable to create pipes?");
        libc.exit(1);
    }

    /* Stuff information into the pipe. */
    for( int fd = 0; fd < fd_limit(); fd += 1 )
    {
        fdinfo_t *info = fd_lookup(fd);
        if( info != NULL &&
           (info->type == BOUND || info->type == SAVED) )
        {
            if( info_encode(pipes[1], fd, info) < 0 )
            {
                DEBUG("Error encoding fd %d: %s",
                      fd, strerror(errno));
            }
            else
            {
                DEBUG("Encoded fd %d (type %d).", fd, info->type);
            }
        }
    }
    libc.close(pipes[1]);
    DEBUG("Finished encoding.");

    /* Prepare our environment variable. */
    char pipe_env[32];
    snprintf(pipe_env, 32, "HUPTIME_PIPE=%d", pipes[0]);

    /* Mask the existing environment variable. */
    char **environ = environ_copy;
    int environ_len = 0;

    for( environ_len = 0;
         environ[environ_len] != NULL;
         environ_len += 1 )
    {
        if( !strncmp("HUPTIME_PIPE=",
                     environ[environ_len], 
                     strlen("HUPTIME_PIPE=")) )
        {
            environ[environ_len] = pipe_env;
            break;
        }
    }

    /* Do we need to extend the environment? */
    if( environ[environ_len] == NULL )
    {
        char** new_environ = malloc(sizeof(char*) * (environ_len + 2));
        memcpy(new_environ, environ, sizeof(char*) * (environ_len));
        new_environ[environ_len] = pipe_env;
        new_environ[environ_len + 1] = NULL;
        environ = new_environ;
    }

    /* Execute in the same environment, etc. */
    chdir(cwd_copy);
    DEBUG("Doing exec()... bye!");
    execve(exe_copy, args_copy, environ);

    /* Bail. Should never reach here. */
    DEBUG("Things went horribly wrong!");
    libc.exit(1);
}

void
impl_exit_check(void)
{
    if( is_exiting == TRUE && total_tracked == 0 )
    {
        if( wait_mode == TRUE )
        {
            /* Check for any active child processes.
             * NOTE: Because we are using waitid() here, and
             * that allows us to specify WNOWAIT, the child
             * will stay in a waitable state for to be reaped
             * whenever the actual program wants to. */
            do {
                siginfo_t info;
                int rval = waitid(P_ALL, 0, &info, WNOHANG|WNOWAIT);
                if( rval < 0 && errno == EINTR )
                {
                    continue;
                }
                if( rval >= 0 || (rval < 0 && errno != ECHILD) )
                {
                    /* There are still active child processes. */
                    return;
                }
                break;
            } while( 1 );
        }

        DEBUG("No active connections, finishing exit.");

        switch( exit_strategy )
        {
            case FORK:
                /* We're done.
                 * No more connections are active, and there's
                 * presumably already a child process handling 
                 * new incoming connections. */
                DEBUG("Goodbye!");
                libc.exit(0);
                break;

            case EXEC:
                /* Let's do the exec.
                 * We're wrapped up existing connections, we can
                 * re-execute the application to start handling new
                 * incoming connections. */
                DEBUG("See you soon...");
                impl_exec();
                break;
        }
    }
}

static int
info_close(int fd, fdinfo_t* info)
{
    int rval = -1;

    switch( info->type )
    {
        case BOUND:
        case TRACKED:
            if( info->type == BOUND && revive_mode == TRUE )
            {
                /* We don't close bound sockets in revive mode.
                 * This allows the program to exit "cleanly" and
                 * we will preserve the socket for the next run. */
                rval = 0;
                break;
            }
            dec_ref(info);
            fd_delete(fd);
            rval = libc.close(fd);
            break;

        case SAVED:
        case DUMMY:
            /* Woah, their program is most likely either messed up,
             * or it's going through and closing all descriptors
             * prior to an exec. We're just going to ignore this. */
            break;
    }

    return rval;
}

static int
do_dup3(int fd, int fd2, int flags)
{
    int rval = -1;
    fdinfo_t *info = NULL;
    fdinfo_t *info2 = NULL;

    DEBUG("do_dup3(%d, %d, ...) ...", fd, fd2);
    L();
    if( fd == fd2 )
    {
        U();
        DEBUG("do_dup3(%d, %d, ...) => 0", fd, fd2);
        return fd2;
    }

    info = fd_lookup(fd);
    info2 = fd_lookup(fd2);
    if( info2 != NULL )
    {
        rval = info_close(fd2, info2);
        if( rval < 0 )
        {
            U();
            DEBUG("do_dup3(%d, %d, ...) => %d (close failed)", fd, fd2, rval);
            return rval;
        }
    }

    rval = libc.dup3(fd, fd2, flags);
    if( rval < 0 )
    {
        U();
        DEBUG("do_dup3(%d, %d, ...) => %d (dup3 failed)", fd, fd2, rval);
        return rval;
    }

    if( info != NULL )
    {
        inc_ref(info);
        fd_save(fd2, info);
    }

    U();
    DEBUG("do_dup3(%d, %d, ...) => %d", fd, fd2, rval);
    return rval;
}

static int
do_dup2(int fd, int fd2)
{
    return do_dup3(fd, fd2, 0);
}

static int
do_close(int fd)
{
    int rval = -1;
    fdinfo_t *info = NULL;

    DEBUG("do_close(%d, ...) ...", fd);
    L();
    info = fd_lookup(fd);
    if( info == NULL )
    {
        U();
        rval = libc.close(fd);
        DEBUG("do_close(%d) => %d (no info)", fd, rval);
        return rval;
    }

    rval = info_close(fd, info);
    impl_exit_check();
    U();

    DEBUG("do_close(%d) => %d (%d tracked)",
        fd, rval, total_tracked);
    return rval;
}

static void
impl_init_lock(void)
{
    /* Initialize our lock.
     * This is a recursive lock simply for convenience.
     * There are a few calls (i.e. bind) which leverage
     * other unlock internal calls (do_dup2), so we make
     * the lock recursive. This could easily be eliminated
     * with a little bit of refactoring. */
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&mutex, &mutex_attr);
}

static void
impl_init_thread(void)
{
    /* Create our restart thread.
     *
     * See the note in sighandler() for an explanation
     * of why the restart must be done in a separate thread.
     *
     * We do the thread creation here instead of in the
     * handler because pthread_create() is not a signal-safe
     * function to call from the handler. */
    if( pipe(restart_pipe) < 0 )
    {
        DEBUG("Error creating restart pipes: %s", strerror(errno));
        libc.exit(1);
    }

    /* Ensure that we have cloexec. */
    if( fcntl(restart_pipe[0], F_SETFD, FD_CLOEXEC) < 0 ||
        fcntl(restart_pipe[1], F_SETFD, FD_CLOEXEC) < 0 )
    {
        DEBUG("Can't set restart pipe to cloexec?");
        libc.exit(1);
    }

    pthread_t thread;
    pthread_attr_t thread_attr;
    pthread_attr_init(&thread_attr);
    pthread_attr_setdetachstate(&thread_attr, 1);
    if( pthread_create(&thread, &thread_attr, impl_restart_thread, NULL) < 0 )
    {
        DEBUG("Error creating restart thread: %s", strerror(errno));
        libc.exit(1);
    }
}

void
impl_init(void)
{
    const char* mode_env = getenv("HUPTIME_MODE");
    const char* multi_env = getenv("HUPTIME_MULTI");
    const char* revive_env = getenv("HUPTIME_REVIVE");
    const char* debug_env = getenv("HUPTIME_DEBUG");
    const char* pipe_env = getenv("HUPTIME_PIPE");
    const char* wait_env = getenv("HUPTIME_WAIT");

    if( debug_env != NULL && strlen(debug_env) > 0 )
    {
        debug_enabled = !strcasecmp(debug_env, "true") ? TRUE: FALSE;
    }

    DEBUG("Initializing...");

    /* Initialize our lock. */
    impl_init_lock();

    /* Save this pid as our master pid.
     * This is done to handle processes that use
     * process pools. We remember the master pid and
     * will do the full fork()/exec() only when we are
     * the master. Otherwise, we will simply shutdown
     * gracefully, and all the master to restart. */
    master_pid = getpid();

    /* Grab our exit strategy. */
    if( mode_env != NULL && strlen(mode_env) > 0 )
    {
        if( !strcasecmp(mode_env, "fork") )
        {
            exit_strategy = FORK;
            DEBUG("Exit strategy is fork.");
        }
        else if( !strcasecmp(mode_env, "exec") )
        {
            exit_strategy = EXEC;
            DEBUG("Exit strategy is exec.");
        }
        else
        {
            fprintf(stderr, "Unknown exit strategy.");
            libc.exit(1);
        }
    }

    /* Check if we have something to unlink. */
    to_unlink = getenv("HUPTIME_UNLINK");
    if( to_unlink != NULL && strlen(to_unlink) > 0 )
    {
        DEBUG("Unlink is '%s'.", to_unlink);
    }

    /* Clear up any outstanding child processes.
     * Because we may have exited before the process
     * could do appropriate waitpid()'s, we try to
     * clean up children here. Note that we may have
     * some zombies that hang around during the life
     * of the program, but at every restart they will
     * be cleaned up (so at least they won't grow
     * without bound). */
    int status = 0;
    while( waitpid((pid_t)-1, &status, WNOHANG) > 0 );

    /* Check if we're in multi mode. */
    if( multi_env != NULL && strlen(multi_env) > 0 )
    {
        multi_mode = !strcasecmp(multi_env, "true") ? TRUE: FALSE;
    }
#ifndef SO_REUSEPORT
    if( multi_mode == TRUE )
    {
        fprintf(stderr, "WARNING: Multi mode not supported.\n");
        fprintf(stderr, "(Requires at least Linux 3.9 and recent headers).\n");
    } 
#endif

    /* Check if we're in revive mode. */
    if( revive_env != NULL && strlen(revive_env) > 0 )
    {
        revive_mode = !strcasecmp(revive_env, "true") ? TRUE : FALSE;
    }

    /* Check if we are in wait mode. */
    if( wait_env != NULL && strlen(wait_env) > 0 )
    {
        wait_mode = !strcasecmp(wait_env, "true") ? TRUE : FALSE;
    }

    /* Check if we're a respawn. */
    if( pipe_env != NULL && strlen(pipe_env) > 0 )
    {
        int fd = -1;
        fdinfo_t *info = NULL;
        int pipefd = strtol(pipe_env, NULL, 10);

        DEBUG("Loading all file descriptors.");

        /* Decode all passed information. */
        while( !info_decode(pipefd, &fd, &info) )
        {
            fd_save(fd, info);
            DEBUG("Decoded fd %d (type %d).", fd, info->type);
            info = NULL;
        }
        if( info != NULL )
        {
            dec_ref(info);
        }

        /* Finished with the pipe. */
        libc.close(pipefd);
        unsetenv("HUPTIME_PIPE");
        DEBUG("Finished decoding.");

        /* Close all non-encoded descriptors. */
        for( fd = 0; fd < fd_max(); fd += 1 )
        {
            info = fd_lookup(fd);
            if( info == NULL )
            {
                DEBUG("Closing fd %d.", fd);
                libc.close(fd);
            }
        }

        /* Restore all given file descriptors. */
        for( fd = 0; fd < fd_limit(); fd += 1 )
        {
            info = fd_lookup(fd);
            if( info != NULL && info->type == SAVED )
            {
                fdinfo_t *orig_info = fd_lookup(info->saved.fd);
                if( orig_info != NULL )
                {
                    /* Uh-oh, conflict. Move the original (best effort). */
                    do_dup(info->saved.fd);
                    do_close(info->saved.fd);
                }

                /* Return the offset (ignore failure). */
                if( info->saved.offset != (off_t)-1 )
                {
                    lseek(fd, info->saved.offset, SEEK_SET);
                }

                /* Move the SAVED fd back. */
                libc.dup2(fd, info->saved.fd);
                DEBUG("Restored fd %d.", info->saved.fd);
            }
        }
    }
    else
    {
        DEBUG("Saving all initial file descriptors.");

        /* Save all of our initial files. These are used
         * for re-execing the process. These are persisted
         * effectively forever, and on restarts we close
         * everything that is not a BOUND socket or a SAVED
         * file descriptor. */
        for( int fd = 0; fd < fd_max(); fd += 1 )
        {
            fdinfo_t *info = fd_lookup(fd);
            if( info != NULL )
            {
                /* Encoded earlier. */
                continue;
            }

            /* Make a new SAVED FD. */
            int newfd = libc.dup(fd);
            if( newfd >= 0 )
            {
                fdinfo_t *saved_info = alloc_info(SAVED);

                if( saved_info != NULL )
                {
                    saved_info->saved.fd = fd;
                    saved_info->saved.offset = lseek(fd, 0, SEEK_CUR);
                    fd_save(newfd, saved_info);
                    DEBUG("Saved fd %d (offset %lld).",
                        fd, (long long int)saved_info->saved.offset);
                }
            }
        }
    }

    /* Save the environment.
     *
     * NOTE: We reserve extra space in the environment
     * for our special start-up parameters, which will be added
     * in impl_exec() below. (The encoded BOUND/SAVED sockets).
     *
     * We also filter out the special variables above that were
     * used to pass in information about sockets that were bound. */
    free(environ_copy);
    environ_copy = (char**)read_nul_sep("/proc/self/environ");
    DEBUG("Saved environment.");

    /* Save the arguments. */
    free(args_copy);
    args_copy = (char**)read_nul_sep("/proc/self/cmdline");
    DEBUG("Saved args.");
    for( int i = 0; args_copy[i] != NULL; i += 1 )
    {
        DEBUG(" arg%d=%s", i, args_copy[i]);
    }

    /* Save the cwd & exe. */
    free(cwd_copy);
    cwd_copy = (char*)read_link("/proc/self/cwd");
    DEBUG("Saved cwd.");
    free(exe_copy);
    exe_copy = (char*)read_link("/proc/self/exe");
    DEBUG("Saved exe.");

    /* Initialize our thread. */
    impl_init_thread();

    /* Install our signal handlers.
     * We also ensure that they are unmasked. This
     * is important because we may have specifically
     * masked the signals prior to the exec() below,
     * to cover the race between program start and 
     * us installing the appropriate handlers. */
    struct sigaction action;
    action.sa_handler = sighandler;
    action.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &action, NULL);
    sigaction(SIGUSR2, &action, NULL);
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGUSR2);
    sigprocmask(SIG_UNBLOCK, &set, NULL);
    DEBUG("Installed signal handlers.");

    /* Done. */
    DEBUG("Initialization complete.");
}

static int
impl_dummy_server(void)
{
    int dummy_server = -1;

    /* Create our dummy sock. */
    struct sockaddr_un dummy_addr;
    char *socket_path = tempnam("/tmp", ".huptime");

    memset(&dummy_addr, 0, sizeof(struct sockaddr_un));
    dummy_addr.sun_family = AF_UNIX;
    strncpy(dummy_addr.sun_path, socket_path, sizeof(dummy_addr.sun_path)-1);

    /* Create a dummy server. */
    dummy_server = socket(AF_UNIX, SOCK_STREAM, 0);
    if( dummy_server < 0 )
    {
        fprintf(stderr, "Unable to create unix socket?");
        return -1;
    }
    if( fcntl(dummy_server, F_SETFD, FD_CLOEXEC) < 0 )
    {
        close(dummy_server);
        fprintf(stderr, "Unable to set cloexec?");
        return -1;
    }
    if( libc.bind(
            dummy_server,
            (struct sockaddr*)&dummy_addr,
            sizeof(struct sockaddr_un)) < 0 )
    {
        close(dummy_server);
        fprintf(stderr, "Unable to bind unix socket?");
        return -1;
    }
    if( libc.listen(dummy_server, 1) < 0 )
    {
        close(dummy_server);
        fprintf(stderr, "Unable to listen on unix socket?");
        return -1;
    }

    /* Connect a dummy client. */
    int dummy_client = socket(AF_UNIX, SOCK_STREAM, 0);
    if( dummy_client < 0 )
    {
        close(dummy_server);
        fprintf(stderr, "Unable to create unix socket?");
        return -1;
    }
    if( fcntl(dummy_client, F_SETFD, FD_CLOEXEC) < 0 )
    {
        close(dummy_server);
        close(dummy_client);
        fprintf(stderr, "Unable to set cloexec?");
        return -1;
    }
    if( connect(
            dummy_client,
            (struct sockaddr*)&dummy_addr,
            sizeof(struct sockaddr_un)) < 0 )
    {
        close(dummy_server);
        close(dummy_client);
        fprintf(stderr, "Unable to connect dummy client?");
        return -1;
    }

    /* Put the client into an error state. */
    int dummy_fd = libc.accept(dummy_server, NULL, 0);
    if( dummy_fd < 0 )
    {
        fprintf(stderr, "Unable to accept internal client?");
        close(dummy_server);
        close(dummy_client);
        return -1;
    }
    close(dummy_fd);

    /* Save the dummy info. */
    fdinfo_t* dummy_info = alloc_info(DUMMY);
    if( dummy_info == NULL )
    {
        fprintf(stderr, "Unable to allocate dummy info?");
        return -1;
    }
    dummy_info->dummy.client = dummy_client;
    fd_save(dummy_server, dummy_info);
    inc_ref(dummy_info);
    fd_save(dummy_client, dummy_info);

    /* Ensure that it's unlinked. */
    unlink(socket_path);
    free(socket_path);

    return dummy_server;
}

void
impl_exit_start(void)
{
    if( is_exiting == TRUE )
    {
        return;
    }

    /* We are now exiting.
     * After this point, all calls to various sockets,
     * (i.e. accept(), listen(), etc. will result in stalls.
     * We are just waiting until existing connections have 
     * finished and then we will be either exec()'ing a new
     * version or exiting this process. */
    is_exiting = TRUE;

    /* Get ready to restart.
     * We only proceed with actual restart actions
     * if we are the master process, otherwise we will
     * simply prepare to shutdown cleanly once all the
     * current active connections have finished. */
    if( master_pid == getpid() )
    {
        pid_t child;
        DEBUG("Exit started -- this is the master.");

        /* Unlink files (e.g. pidfile). */
        if( to_unlink != NULL && strlen(to_unlink) > 0 )
        {
            DEBUG("Unlinking '%s'...", to_unlink);
            unlink(to_unlink);
        }

        /* Neuther this process. */
        for( int fd = 0; fd < fd_limit(); fd += 1 )
        {
            fdinfo_t* info = fd_lookup(fd);
            if( exit_strategy == FORK &&
                info != NULL && info->type == SAVED )
            {
                /* Close initial files. Since these
                 * are now passed on to the child, we
                 * ensure that the parent won't mess 
                 * with them anymore. Note that we still
                 * have a copy as all SAVED descriptors. */
                if( info->saved.fd == 2 )
                {
                    /* We treat stderr special.
                     * Assuming logging will go here, we
                     * allow the parent process to continue
                     * writing to this file (and hope that
                     * it's open in APPEND mode, etc.). */
                    continue;
                }
                int nullfd = open("/dev/null", O_RDWR);
                do_dup2(nullfd, info->saved.fd);
                libc.close(nullfd);
            }
            if( info != NULL &&
                info->type == BOUND && !info->bound.is_ghost )
            {
                /* Change BOUND sockets to dummy sockets.
                 * This will allow select() and poll() to
                 * operate as you expect, and never give
                 * back new clients. */
                int newfd = do_dup(fd);
                if( newfd >= 0 )
                {
                    int dummy_server = impl_dummy_server();
                    if( dummy_server >= 0 )
                    {
                        info->bound.is_ghost = 1;
                        do_dup2(dummy_server, fd);
                        DEBUG("Replaced FD %d with dummy.", fd);
                    }
                    else
                    {
                        do_close(newfd);
                    }
                }
            }
        }

        switch( exit_strategy )
        {
            case FORK:
                /* Start the child process.
                 * We will exit gracefully when the tracked
                 * connection count reaches zero. */
                DEBUG("Exit strategy is fork.");
                child = libc.fork();
                if( child == 0 )
                {
                    DEBUG("I'm the child.");
                    impl_exec();
                }
                else
                {
                    DEBUG("I'm the parent.");
                    master_pid = child;
                }
                break;

            case EXEC:
                /* Nothing necessary beyond the above. */
                DEBUG("Exit strategy is exec.");
                break;
        }
    }
    else
    {
        /* Force our strategy to fork, though we haven't forked.
         * This will basically just have this process exit cleanly
         * once all the current active connections have finished. */
        DEBUG("Exit started -- this is the child.");
        exit_strategy = FORK;
    }
}

void
impl_restart(void)
{
    /* Indicate that we are now exiting. */
    L();
    impl_exit_start();
    impl_exit_check();
    U();
}

void*
impl_restart_thread(void* arg)
{
    /* Wait for our signal. */
    while( 1 )
    {
        char go = 0;
        int rc = read(restart_pipe[0], &go, 1);
        if( rc == 1 )
        {
            /* Go. */
            break;
        }
        else if( rc == 0 )
        {
            /* Wat? Restart. */
            DEBUG("Restart pipe closed?!");
            break;
        }
        else if( rc < 0 && (errno == EAGAIN || errno == EINTR) )
        {
            /* Keep trying. */
            continue;
        }
        else
        {
            /* Real error. Let's restart. */
            DEBUG("Restart pipe fubared?!");
            break;
        }
    }

    libc.close(restart_pipe[0]);
    restart_pipe[0] = -1;

    /* See note above in sighandler(). */
    impl_restart();
    return arg;
}

static pid_t
do_fork(void)
{
    pid_t res = (pid_t)-1;

    /* We block SIGHUP during fork().
     * This is because we communicate our restart
     * intention via a pipe, and it's conceivable
     * that between the fork() and impl_init_thread()
     * the signal handler will be triggered and we'll
     * end up writing to the restart pipe that is
     * still connected to the master process. */
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGHUP);
    sigprocmask(SIG_BLOCK, &set, NULL);

    DEBUG("do_fork() ...");

    L();
    res = libc.fork();
    if( res == 0 )
    {
        if( total_bound == 0 )
        {
            /* We haven't yet bound any sockets. This is
             * a common pattern where the process may be
             * daemonizing. We reset the master_pid so that
             * the initalization routine will actually reset
             * and treat this new process as a master.
             * The reason we don't do this if sockets are
             * already bound, is that if master_pid != getpid(),
             * i.e. for process pools, then we neither fork()
             * nor exec(), but simply go into a normal exit. */
            master_pid = getpid();
        }

        impl_init_lock();
        impl_init_thread();
    }
    else
    {
        U();
    }

    sigprocmask(SIG_UNBLOCK, &set, NULL);
    DEBUG("do_fork() => %d", res);
    return res;
}

static int
do_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    fdinfo_t *info = NULL;
    int rval = -1;

    DEBUG("do_bind(%d, ...) ...", sockfd);
    L();

    /* See if this socket already exists. */
    for( int fd = 0; fd < fd_limit(); fd += 1 )
    {
        fdinfo_t *info = fd_lookup(fd);
        if( info != NULL && 
            info->type == BOUND &&
            info->bound.addrlen == addrlen &&
            !memcmp(addr, (void*)&info->bound.addr, addrlen) )
        {
            DEBUG("Found ghost %d, cloning...", fd);

            /* Give back a duplicate of this one. */
            int rval = do_dup2(fd, sockfd);
            if( rval < 0 )
            {
                /* Dup2 failed? */
                DEBUG("Failed.");
                continue;
            }
            if( info->bound.is_ghost )
            {
                /* Close the original (not needed). */
                info->bound.is_ghost = 0;
                do_close(fd);
            }

            /* Success. */
            U();
            DEBUG("do_bind(%d, ...) => 0 (ghosted)", sockfd);
            return 0;
        }
    }

#ifdef SO_REUSEPORT
    /* Multi mode? Set socket options. */
    if( multi_mode == TRUE )
    {
        int optval = 1;
        if( setsockopt(sockfd,
                       SOL_SOCKET,
                       SO_REUSEPORT,
                       &optval,
                       sizeof(optval)) < 0 )
        {
            U();
            DEBUG("do_bind(%d, ...) => -1 (no multi?)", sockfd);
            return -1;
        }

        DEBUG("Multi mode enabled.");
    }
#endif

    /* Try a real bind. */
    info = alloc_info(BOUND);
    if( info == NULL )
    {
        U();
        DEBUG("do_bind(%d, ...) => -1 (alloc error?)", sockfd);
        return -1;
    }
    rval = libc.bind(sockfd, addr, addrlen);
    if( rval < 0 )
    {
        dec_ref(info);
        U();
        DEBUG("do_bind(%d, ...) => %d (error)", sockfd, rval);
        return rval;
    }

    /* Ensure that this socket is non-blocking,
     * this is because we override the behavior
     * for accept() and we require non-blocking
     * behavior. We deal with the consequences. */
    rval = fcntl(sockfd, F_SETFL, O_NONBLOCK);
    if( rval < 0 )
    {
        dec_ref(info);
        U();
        DEBUG("do_bind(%d, ...) => %d (fcntl error)", sockfd, rval);
        return -1;
    }

    /* Save a refresh bound socket info. */
    info->bound.stub_listened = 0;
    info->bound.real_listened = 0;
    memcpy((void*)&info->bound.addr, (void*)addr, addrlen);
    info->bound.addrlen = addrlen;
    fd_save(sockfd, info);

    /* Success. */
    U();
    DEBUG("do_bind(%d, ...) => %d", sockfd, rval);
    return rval;
}

static int
do_listen(int sockfd, int backlog)
{
    int rval = -1;
    fdinfo_t *info = NULL;

    DEBUG("do_listen(%d, ...) ...", sockfd);
    L();
    info = fd_lookup(sockfd);
    if( info == NULL || info->type != BOUND )
    {
        U();
        DEBUG("do_listen(%d, %d) => -1 (not BOUND)", sockfd, backlog);
        errno = EINVAL;
        return -1;
    }

    /* Check if we can short-circuit this. */
    if( info->bound.real_listened )
    {
        info->bound.stub_listened = 1;
        U();
        DEBUG("do_listen(%d, %d) => 0 (stub)", sockfd, backlog);
        return 0;
    }

    /* Can we really call listen() ? */
    if( is_exiting == TRUE )
    {
        info->bound.stub_listened = 1;
        U();
        DEBUG("do_listen(%d, %d) => 0 (is_exiting)", sockfd, backlog);
        return 0;
    }

    /* We largely ignore the backlog parameter. People 
     * don't really use sensible values here for the most
     * part. Hopefully (as is default on some systems),
     * tcp syn cookies are enabled, and there's no real
     * limit for this queue and this parameter is silently
     * ignored. If not, then we use the largest value we 
     * can sensibly use. */
    (void)backlog;
    rval = libc.listen(sockfd, SOMAXCONN);
    if( rval < 0 )
    {
        U();
        DEBUG("do_listen(%d, %d) => %d", sockfd, backlog, rval);
        return rval;
    }

    /* We're done. */
    info->bound.real_listened = 1;
    info->bound.stub_listened = 1;
    U();
    DEBUG("do_listen(%d, %d) => %d", sockfd, backlog, rval);
    return rval;
}

static int
do_accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
    int rval = -1;
    fdinfo_t *info = NULL;

    DEBUG("do_accept4(%d, ...) ...", sockfd);
    L();
    info = fd_lookup(sockfd);
    if( info == NULL || (info->type != BOUND && info->type != DUMMY) )
    {
        U();
        /* Should return an error. */
        rval = libc.accept4(sockfd, addr, addrlen, flags);
        DEBUG("do_accept4(%d, ...) => %d (no info)", sockfd, rval);
        return rval;
    }

    /* Check that they've called listen. */
    if( info->type == BOUND && !info->bound.stub_listened )
    {
        U();
        DEBUG("do_accept4(%d, ...) => -1 (not listened)", sockfd);
        errno = EINVAL;
        return -1;
    }

    /* Check if this is a dummy.
     * There's no way that they should be calling accept().
     * The dummy FD will never trigger a poll, select, epoll,
     * etc. So we just act as a socket with no clients does --
     * either return immediately or block forever. NOTE: We
     * still return in case of EINTR or other suitable errors. */
    if( info->type == DUMMY && info->dummy.client >= 0 )
    {
        rval = info->dummy.client;
        info->dummy.client = -1;
        U();
        DEBUG("do_accept4(%d, ...) => %d (dummy client)", sockfd, rval);
        return rval;
    }

    U();

    /* Wait for activity on the socket. */
    struct pollfd poll_info;
    poll_info.fd = sockfd;
    poll_info.events = POLLIN;
    poll_info.revents = 0;
    if( poll(&poll_info, 1, -1) < 0 )
    {
        return -1;
    }

    L();

    /* Check our status. */
    if( is_exiting == TRUE )
    {
        /* We've transitioned from not exiting
         * to exiting in this period. This will
         * circle around a return a dummy descriptor. */
        U();
        errno = EINTR;
        return -1;
    }

    /* Do the accept for real. */
    fdinfo_t *new_info = alloc_info(TRACKED);
    if( new_info == NULL )
    {
        U();
        DEBUG("do_accept4(%d, ...) => -1 (alloc error?)", sockfd);
        return -1;
    }
    inc_ref(info);
    new_info->tracked.bound = info;
    rval = libc.accept4(sockfd, addr, addrlen, flags);

    if( rval >= 0 )
    {
        /* Save the reference to the socket. */
        fd_save(rval, new_info);
    }
    else
    {
        /* An error occured, nothing to track. */
        dec_ref(new_info);

        if( errno == EWOULDBLOCK && !(flags & SOCK_NONBLOCK) )
        {
        }
    }

    U();
    DEBUG("do_accept4(%d, ...) => %d (tracked %d)",
        sockfd, rval, total_tracked);
    return rval;
}

static int
do_accept4_retry(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
    while (1)
    {
        int rval = do_accept4(sockfd, addr, addrlen, flags);
        if( rval < 0 && (errno == EAGAIN || errno == EINTR) )
        {
            /* Signal interrupted the system call.
             * Many programs cannot handle this cleanly,
             * (hence why they are using huptime). So we
             * simply absorb this error and continue. */
            continue;
        }

        /* Otherwise, give the error back. */
        return rval;
    }
}

static int
do_accept_retry(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    return do_accept4_retry(sockfd, addr, addrlen, 0);
}

static void
do_exit(int status)
{
    if( revive_mode == TRUE )
    {
        DEBUG("Reviving...");
        impl_exec();
    }

    libc.exit(status);
}

static pid_t
do_wait(void *status)
{
    pid_t rval = libc.wait(status);
    L();
    impl_exit_check();
    U();
    return rval;
}

static pid_t
do_waitpid(pid_t pid, int *status, int options)
{
    pid_t rval = libc.waitpid(pid, status, options);
    L();
    impl_exit_check();
    U();
    return rval;
}

funcs_t impl =
{
    .bind = do_bind,
    .listen = do_listen,
    .accept = do_accept_retry,
    .accept4 = do_accept4_retry,
    .close = do_close,
    .fork = do_fork,
    .dup = do_dup,
    .dup2 = do_dup2,
    .dup3 = do_dup3,
    .exit = do_exit,
    .wait = do_wait,
    .waitpid = do_waitpid,
};
funcs_t libc;
