/*
 * impl.c
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

#define L() pthread_mutex_lock(&mutex)
#define U() pthread_mutex_unlock(&mutex)

/* Our core signal handlers. */
static void* impl_restart_thread(void*);
void
sighandler(int signo)
{
    DEBUG("SIGNAL CAUGHT.");

    /* Fire off a restart thread.
     * We have to do this in a separate thread, because
     * we have no guarantees about which thread has been
     * interrupted in order to execute this signal handler.
     * Because this could have happened during a critical
     * section (i.e. locks held) we have no choice but to
     * fire the restart asycnhronously so that it too can
     * grab locks appropriately. */
    pthread_t thread;
    pthread_attr_t thread_attr;
    pthread_attr_init(&thread_attr);
    pthread_attr_setdetachstate(&thread_attr, 1);
    pthread_create(&thread, &thread_attr, impl_restart_thread, NULL);
}

static int
do_dup(int fd)
{
    int rval = -1;
    fdinfo_t *info = NULL;
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
     * past Linux 2.6.11 (IIRC) this is 65K.
     */
    int pipes[2];
    if( pipe(pipes) < 0 )
    {
        DEBUG("Unable to create pipes?");
        _exit(1);
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
    DEBUG("Doing exec()... bye!");
    execve(exe_copy, args_copy, environ);

    /* Bail. Should never reach here. */
    DEBUG("Things went horribly wrong!");
    _exit(1);
}

void
impl_exit_check(void)
{
    if( is_exiting == TRUE && total_tracked == 0 )
    {
        DEBUG("No active connections, finishing exit.");

        switch( exit_strategy )
        {
            case FORK:
                /* We're done.
                 * No more connections are active, and there's
                 * presumably already a child process handling 
                 * new incoming connections. */
                DEBUG("Goodbye!");
                _exit(0);
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
            dec_ref(info);
            fd_delete(fd);
            rval = libc.close(fd);
            break;

        case SAVED:
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
    if( fd == fd2 )
    {
        return fd2;
    }

    L();
    info = fd_lookup(fd);
    info2 = fd_lookup(fd2);
    if( info2 != NULL )
    {
        rval = info_close(fd2, info2);
        if( rval < 0 )
        {
            U();
            return rval;
        }
    }

    rval = libc.dup3(fd, fd2, flags);
    if( rval < 0 )
    {
        U();
        return rval;
    }

    if( info != NULL )
    {
        inc_ref(info);
        fd_save(fd2, info);
    }

    U();
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
    L();
    info = fd_lookup(fd);
    if( info == NULL )
    {
        U();
        rval = libc.close(fd);
        return rval;
    }

    rval = info_close(fd, info);
    impl_exit_check();
    U();

    DEBUG("do_close(%d) => %d (with info)", fd, rval);
    return rval;
}

void
impl_init(void)
{
    const char* mode_env = getenv("HUPTIME_MODE");
    const char* multi_env = getenv("HUPTIME_MULTI");
    const char* debug_env = getenv("HUPTIME_DEBUG");
    const char* pipe_env = getenv("HUPTIME_PIPE");

    if( debug_env != NULL && strlen(debug_env) > 0 )
    {
        debug_enabled = !strcasecmp(debug_env, "true") ? TRUE: FALSE;
    }

    DEBUG("Initializing...");
    
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
            DEBUG("Unknown exit strategy.");
            _exit(1);
        }
    }

    /* Check if we have something to unlink. */
    to_unlink = getenv("HUPTIME_UNLINK");
    if( to_unlink != NULL && strlen(to_unlink) > 0 )
    {
        DEBUG("Unlink is '%s'.", to_unlink);
    }

    /* Check if we're in multi mode. */
    if( multi_env != NULL && strlen(multi_env) > 0 )
    {
        multi_mode = !strcasecmp(multi_env, "true") ? TRUE: FALSE;
    }
#ifndef SO_REUSEPORT
    if( multi_mode == TRUE )
    {
        fprintf(stderr, "Multi mode not supported.\n");
        fprintf(stderr, "(Requires at least Linux 3.9 and recent headers).\n");
        _exit(1);
    } 
#endif

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
                fdinfo_t *info = alloc_info(SAVED);
                if( info != NULL )
                {
                    info->saved.fd = fd;
                    fd_save(newfd, info);
                    DEBUG("Saved fd %d.", fd);
                    continue;
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

    /* Save the cwd & exe. */
    free(cwd_copy);
    cwd_copy = (char*)read_link("/proc/self/cwd");
    DEBUG("Saved cwd.");
    free(exe_copy);
    exe_copy = (char*)read_link("/proc/self/exe");
    DEBUG("Saved exe.");

    /* Install our signal handlers.
     * We also ensure that they are unmasked. This
     * is important because we may have specifically
     * masked the signals prior to the exec() below,
     * to cover the race between program start and 
     * us installing the appropriate handlers. */
    signal(SIGHUP, sighandler);
    signal(SIGUSR2, sighandler);
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGUSR2);
    sigprocmask(SIG_UNBLOCK, &set, NULL);
    DEBUG("Installed signal handlers.");

    /* Done. */
    DEBUG("Initialization complete.");
}

void
impl_exit_start(void)
{
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
                }
                break;

            case EXEC:
                /* Do nothing.
                 * We will exec gracefully when the tracked
                 * connection count reaches zero. */
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

    /* We are now exiting.
     * After this point, all calls to various sockets,
     * (i.e. accept(), listen(), etc. will result in stalls.
     * We are just waiting until existing connections have 
     * finished and then we will be either exec()'ing a new
     * version or exiting this process. */
    is_exiting = TRUE;
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
    /* See note above in sighanlder(). */
    impl_restart();
    return arg;
}

static pid_t
do_fork(void)
{
    pid_t res = (pid_t)-1;
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
    }

    U();
    return res;
}

static int
do_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    fdinfo_t *info = NULL;
    int rval = -1;

    DEBUG("Bind called, searching for ghost...");
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
            DEBUG("Success.");
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
            DEBUG("Error enabling multi mode.");
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
        DEBUG("Unable to allocate new info?");
        return -1;
    }
    rval = libc.bind(sockfd, addr, addrlen);
    if( rval < 0 )
    {
        dec_ref(info);
        U();
        DEBUG("do_bind(...) => %d", rval);
        return rval;
    }

    /* Save a refresh bound socket info. */
    info->bound.stub_listened = 0;
    info->bound.real_listened = 0;
    memcpy((void*)&info->bound.addr, (void*)addr, addrlen);
    info->bound.addrlen = addrlen;
    fd_save(sockfd, info);

    /* Success. */
    U();
    DEBUG("do_bind(...) => %d", rval);
    return rval;
}

static int
do_listen(int sockfd, int backlog)
{
    int rval = -1;
    fdinfo_t *info = NULL;
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
    L();
    info = fd_lookup(sockfd);
    if( info == NULL || info->type != BOUND )
    {
        U();
        /* Should return an error. */
        rval = libc.accept4(sockfd, addr, addrlen, flags);
        DEBUG("do_accept4(%d, ...) => %d (no info)", sockfd, rval);
        return rval;
    }

    /* Check that they've called listen. */
    if( !info->bound.stub_listened )
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
    if( is_exiting == TRUE )
    {
        U();

        if( flags & SOCK_NONBLOCK )
        {
            DEBUG("do_accept4(%d, ...) => -1 (would block)", sockfd);
            errno = EWOULDBLOCK;
            return -1;
        }
        else
        {
            while( 1 )
            {
                rval = sleep((unsigned int)-1);
                if( rval != 0 && errno != EINTR && errno != EAGAIN )
                {
                    DEBUG("do_accept4(%d, ...) => %d (sleep)", sockfd, rval);
                    return rval;
                }
            }
        }
    }

    /* Create our new socket. */
    fdinfo_t *new_info = alloc_info(TRACKED);
    if( new_info == NULL )
    {
        U();
        DEBUG("Unable to allocate new info?");
        return -1;
    }
    inc_ref(info);
    new_info->tracked.bound = info;

    /* Do the accept for real. */
    rval = libc.accept4(sockfd, addr, addrlen, flags);
    if( rval >= 0 )
    {
        /* Save the reference to the socket. */
        fd_save(rval, new_info);
    } else {
        /* An error occured, nothing to track. */
        dec_ref(new_info);
    }

    U();
    DEBUG("do_accept4(%d, ...) => %d", sockfd, rval);
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
};
funcs_t libc;
