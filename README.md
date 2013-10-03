Huptime
=======

`Huptime` is a tool for achieving zero downtime restarts without the need to
modify your program in any way. Sweet!

How do I use it?
----------------

You simply need to run your services via `huptime`.

For example:

    # Start the service.
    huptime /usr/bin/myservice &

    # Zero downtime restart.
    killall -HUP myservice

Or, if you use exec:

    # Start the service and get the PID.
    huptime -exec /usr/bin/myservice &
    PID=$!

    # Zero downtime restart (same PID).
    kill -HUP $PID

How does it work?
-----------------

`Huptime` installs a signal handler for `SIGHUP`.

It tracks open file descriptors by intercepting calls to `bind` and `accept`
(among other things). When the program receives a `SIGHUP`, it will
intelligently `exec` a new copy of the program *without* closing any bound
sockets and without requiring any changes to the program.

When the new copy of the program tries to `bind` the same socket, we will
silently replace it with the still-open socket from the previous version.

There are two fundamental modes of operation:

* fork (default)

If you use fork, then when the process receives a `SIGHUP`, then it will `fork`
and `exec` a new copy of the application. This is more efficient, as new
requests can start being served immediately, while old requests are still being
finished by the original program.

This may not integrate cleanly with supervisor processes like `upstart`
however, which depend on the `PID` of the application staying constant.

This may also present issues for some applications that check pidfiles or
contain internal mechanisms for preventing two copies of themselves from
running. `Huptime` goes to some effort to prevent conflict (changing the
cmdline, etc.) but it may still arise.

* exec

If you use exec, then when a process receives a `SIGHUP`, then it will begin
queueing requests to the bound socket and wait until all outstanding requests
are finished. Only when existing requests are finished will the program
restart.

This may not work properly if requests are not bound in how long they will
take. This may also lead to high response times for some clients during the
restart. However, this approach will play well with supervisors.

What's up with the name?
------------------------

It's clever! Services are often reloaded using `SIGHUP`. The point of this tool
is to maximize uptime by enabling zero downtime restarts via `SIGHUP`. Hence,
`huptime`!
