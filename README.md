High uptime
===========

Huptime is a tool for achieving zero downtime restarts without the need to
modify your program in any way.

Although many applications support reloading configurations while running, a
zero downtime restart allows for upgrading the application code without
rejecting any clients.

Basic Example
-------------

In a terminal, run:

    huptime --exec python -m SimpleHTTPServer &
    PID=$!

Then, in a second terminal:

    while true; do curl http://localhost:8080 2>/dev/null || echo "fail"; done

Finally, in a third terminal:

    kill -HUP $PID

You should see no "fail" output on the second terminal.

With this reload, the complete code for SimpleHTTPServer is reloaded
(potentially with changes), but at no time are connections denied or dropped.
When the new version is up and running again (i.e. it binds the socket and
calls accept), then pending connections will be processed.

Why?
----

With continuous deployment, software can be updated dozens, hundreds or even
thousands of times per day. It is critical that service is not interrupted during
upgrades.

In an ideal world, all applications would support a mechanism for doing zero
downtime restarts. The reality is that many standard frameworks make this
difficult to do from the top down. It's not practical to plumb this
functionality through every layer, particularly for applications over which you
have no control.

Compound this with the fact that many applications consist of many different
small components (written using different languages and frameworks), and you've
got yourself a headache.

Because of this complexity, one of the first things people have to do is implement
a custom load balancing tier and a complex upgrade process. Although this is
important at a certain scale, it shouldn't be that hard for simple services.
It's crazy to add a whole new tier when the problem can be solved in a much
simpler way!

Huptime attempts to make it very simple to achieve these restarts for simple,
unmodified applications.

How do I install it?
--------------------

Clone the repo:

    git clone http://github.com/amscanne/huptime

Install it the old-fashioned way:

    cd huptime && sudo make install

Want Ubuntu & Debian packages?

    cd huptime && make deb && dpkg -i huptime*.deb

How about CentOS or RedHat?

    cd huptime && make rpm && rpm -i huptime*.rpm

How do I use it?
----------------

You simply need to run services via huptime.

For example:

    # Start the service.
    huptime /usr/bin/myservice &

    # Zero downtime restart.
    killall -HUP myservice

    # Or, if you prefer...
    huptime --restart /usr/bin/myservice

If there is a pidfile, it can be reset on restart:

    # Start the service.
    huptime --unlink /var/run/myservice.pid /usr/bin/myservice &

    # Zero downtime restarts.
    killall -HUP myservice

    # Again, if you prefer...
    huptime --restart /usr/bin/myservice

Or, if you need exec (for example, to run under upstart):

    # Start the service and get the PID.
    huptime --exec /usr/bin/myservice &
    PID=$!

    # Zero downtime restart (same PID).
    kill -HUP $PID

    # Again, as always...
    huptime --restart /usr/bin/myservice

What does it support?
---------------------

Huptime should [+] handle the following normal things:

* Daemonization & pid files
* Process pools
* Multiple server sockets
* Event-based and thread-based servers
* Integration with supervisors (just use exec!)

In terms of languages and frameworks, huptime should support nearly all
programs that are *dynamically linked* against a *modern libc*.

Most modern dynamic languages (python, ruby, node, etc.) fall into this
category. Most C/C++ programs also fall into this category. A unique exception
is *go*, which invokes system calls directly and uses only static linking.
(For the record, I am a big fan of this approach. However, both have their
merits).

[+] Should. YMMV.

What else does it do?
---------------------

* Transparent multi-binding (running the same service multiple times)

If you are running Linux 3.9+, then you can also easily enable pools of
processes by starting your services with the *--multi* option. Again, this does
not require any modification on your application.

For example:

    # Start the service (4 workers).
    huptime --multi=4 /usr/bin/myservice &

    # Zero downtime restart of all.
    killall -HUP myservice

    # Or, if you prefer...
    huptime --restart /usr/bin/myservice

Want to manage the number of running scripts yourself?

    pids="";

    reload() {
        for pid in $pids; do
            kill -HUP $pid;
        done
    }

    stop() {
        for pid in $pids; do
            kill -TERM $pid;
        done
    }

    trap reload SIGHUP;
    trap stop SIGTERM;
    trap stop SIGINT;

    count="0";
    while [ "$count" -lt "$N" ]; do
        huptime --multi=1 /usr/bin/myservice &
        pids="$pids $!";
        count=$(($count + 1));
    done

    for pid in $pids; do
        wait $pid;
    done

* Transparent restart on exit

If you've got a stubborn program and you want to restart it automatically when
it fails, you should really fix your program. Barring that, you should use a
supervisor like upstart. Barring *that* (you don't care about the sensible
features that a supervisor provides and want zero downtime restarts), you can
use huptime.

To enable this option, simply specify *--revive* on the huptime command line.

For example:

    # Start a zero downtime netcat.
    huptime --revive nc -l 9000 < message.txt &

    # Clients will always find a server...
    nc localhost 9000

How does it work?
-----------------

Huptime installs a signal handler for `SIGHUP`.

It tracks open file descriptors by intercepting calls to `bind` and `accept`
(among other things). When the program receives a `SIGHUP`, it will
intelligently `exec` a new copy of the program *without* closing any bound
sockets and without requiring any changes to the program.

Note that this is not simply a reload but rather a new version of the
application with config changes and code changes (as both now appear on disk).

When the new copy of the program tries to bind the same socket, huptime will
silently replace it with the still-open socket from the previous version.

There are two fundamental modes of operation:

* fork (default)

If you use fork, then when the process receives a `SIGHUP`, then it will `fork`
and `exec` a new copy of the application. This results in less downtime, as new
requests can start being served immediately, while old requests are still being
finished by the original program.

This may not integrate cleanly with supervisor processes like upstart however,
which depend on the PID of the application staying constant.

This may also present issues for some applications that check pidfiles or
contain internal mechanisms for preventing two copies of themselves from
running. Huptime goes to some effort to prevent conflict (allowing for unlink
prior to executing the child), but it may still arise.

* exec

If you use exec, then when a process receives a `SIGHUP`, then it will begin
queueing requests to the bound socket (in the kernel) and wait until all
outstanding requests are finished. Only when existing requests are finished
will the program restart.

This may not work properly if requests are not bounded in how long they will
take. This may also lead to high response times for some clients during the
restart. However, this approach will play well with supervisors.

For example, if you are using upstart, you can do the restart as:

    upstart reload service

Limitations
-----------

Although the majority of programs will work, I'm sure that *all* will not.

The exit is not done through the normal application path on restart. Although
all file descriptors are closed, there may be application-level resources (or
some system resources) that are not cleaned up as expected and may cause
problems.

The command line and environment cannot be changed between restarts. You can
easily work around this issue by putting all configuration inside a file that
is read on start-up (i.e. `myservice --config-file=/etc/myservice.cfg`).

What's up with the name?
------------------------

It's clever! Services are often reloaded using `SIGHUP`. The point of this tool
is to maximize uptime by enabling zero downtime restarts via `SIGHUP`. It's
your high availabilibuddy!
