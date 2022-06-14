---
title: steam-runtime-launcher-service
section: 1
...

<!-- This document:
Copyright © 2020-2021 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

# NAME

steam-runtime-launcher-service - server to launch processes in a container

# SYNOPSIS

**steam-runtime-launcher-service**
[**--exit-on-readable**] *FD*
[**--info-fd**] *N*
[**--replace**]
[**--verbose**]
{**--bus-name** *NAME*|**--socket** *SOCKET*|**--socket-directory** *PATH*}
[**--** *COMMAND* *ARGUMENTS*]

# DESCRIPTION

**steam-runtime-launcher-service** listens on an `AF_UNIX` socket or the
D-Bus session bus, and executes arbitrary commands as subprocesses.

If a *COMMAND* is specified, it is run as an additional subprocess.
It inherits standard input, standard output and standard error
from **steam-runtime-launcher-service** itself.

If the *COMMAND* exits, then the launcher will also exit (as though the
*COMMAND* had been started via **steam-runtime-launch-client --terminate**).

# OPTIONS

**--socket** *PATH*, **--socket** *@ABSTRACT*
:   Listen on the given `AF_UNIX` socket, which can either be an
    absolute path, or `@` followed by an arbitrary string. In both cases,
    the length is limited to 107 bytes and the value must not contain
    ASCII control characters.
    An absolute path indicates a filesystem-based socket, which is
    associated with the filesystem and can be shared between filesystem
    namespaces by bind-mounting.
    The path must not exist before **steam-runtime-launcher-service** is run.
    `@` indicates an an abstract socket, which is associated with a
    network namespace, is shared between all containers that are in
    the same network namespace, and cannot be shared across network
    namespace boundaries.

**--socket-directory** *PATH*
:   Create a filesystem-based socket with an arbitrary name in the
    given directory and listen on that. Clients can use the information
    printed on standard output to connect to the socket. Due to
    limitations of `AF_UNIX` socket addresses, the absolute path to the
    directory must be no more than 64 bytes. It must not contain ASCII
    control characters.

**--bus-name** *NAME*
:   Connect to the well-known D-Bus session bus, request the given name
    and listen for commands there.

**--exit-on-readable** *FD*
:   Exit when file descriptor *FD* (typically 0, for **stdin**) becomes
    readable, meaning either data is available or end-of-file has been
    reached.

**--info-fd** *FD*
:   Print details of the server on *FD* (see **OUTPUT** below).
    This fd will be closed (reach end-of-file) when the server is ready.
    If a *COMMAND* is not specified, the default is standard output,
    equivalent to **--info-fd=1**.
    If a *COMMAND* is specified, there is no default.

**--replace**
:   When used with **--bus-name**, take over the bus name from
    another **steam-runtime-launcher-service** process if any.
    The other **steam-runtime-launcher-service** will exit.
    This option is ignored if **--bus-name** is not used.

**--verbose**
:   Be more verbose.

# ENVIRONMENT

`PWD`
:   Set to the current working directory for each command executed
    inside the container.

`PRESSURE_VESSEL_LOG_INFO` (boolean)
:   If set to `1`, increase the log verbosity up to the info level.
    If set to `0`, no effect.

`PRESSURE_VESSEL_LOG_WITH_TIMESTAMP` (boolean)
:   If set to `1`, prepend the log entries with a timestamp.
    If set to `0`, no effect.

# OUTPUT

**steam-runtime-launcher-service** prints zero or more lines of
structured text on the file descriptor specified by **--info-fd**,
and then closes the **--info-fd**.

If a *COMMAND* is specified, standard output is used for that command's
standard output, and there is no default **--info-fd**.

Otherwise, standard output acts as the default **--info-fd**, and will
be closed at the same time that the **--info-fd** is closed (even if
different).

The text printed to the **--info-fd** includes one or more of these:

**socket=**PATH
:   The launcher is listening on *PATH*, and can be contacted by
    **steam-runtime-launch-client --socket** _PATH_
    or by connecting a peer-to-peer D-Bus client to a D-Bus address
    corresponding to _PATH_.

**dbus_address=**ADDRESS
:   The launcher is listening on *ADDRESS*, and can be contacted by
    **steam-runtime-launch-client --dbus-address** _ADDRESS_,
    or by connecting another peer-to-peer D-Bus client
    (such as **dbus-send --peer=ADDRESS**) to _ADDRESS_.

**bus_name=**NAME
:   The launcher is listening on the well-known D-Bus session bus,
    and can be contacted by
    **steam-runtime-launch-client --bus-name** *NAME*,
    or by connecting an ordinary D-Bus client
    (such as **dbus-send --session**) to the session bus and sending
    method calls to _NAME_.

Before connecting, clients must wait until after at least one of these:

* the **--info-fd** or standard output has been closed
* the bus name (if used) has appeared
* the *COMMAND* (if used) has started

Unstructured diagnostic messages are printed on standard error,
which remains open throughout. If a *COMMAND* is specified, it also
inherits standard error.

# EXIT STATUS

0
:   Terminated gracefully by a signal, by being disconnected from the
    D-Bus session bus after having obtained *NAME* (with **--bus-name**),
    or by *NAME* being replaced by another process
    (with **--bus-name** and **--replace**).

64 (**EX_USAGE** from **sysexits.h**)
:   Invalid arguments were given.

Other nonzero values
:   Startup failed.

# INTENDED USE IN STEAM

## Once per game launch

When a game is launched, run one **steam-runtime-launcher-service** per game
(whatever that means in practice - probably one per app-ID) as an
asynchronous child process, wrapped in **pressure-vessel-wrap**
(either directly or by using **run-in-steamrt** or **run-in-scout**),
with the write end of a **pipe**(2) as its standard output.
Read from the read end of the pipe until EOF is reached,
then leave it running.

Steam should choose a **--socket**, **--socket-directory** or
**--bus-name** according to the granularity required.

If the **realpath**(3) of **~/.steam** is sufficiently short, then Steam
could use a subdirectory of that path as a **--socket-directory**,
but if the user's home directory is very long, that might not work
(the maximum length for a **--socket-directory** is only 64 bytes).

Otherwise, if Steam itself is in a container (for example if
**/.flatpak-info** exists), then it must choose a way to communicate
that can be shared with other containers and the host system. Using the
D-Bus session bus and **--bus-name** is probably the most reliable option
in this case.

If Steam is not in a container, then it can create a random subdirectory
of **/tmp**, for example with **mkdtemp**(3), and either use it as a
**--socket-directory** or create a **--socket** with a meaningful name
in that directory.

In the case of a **--socket-directory**, the socket can be determined by
parsing the data read from standard output.

When the game is to be terminated, Steam should send **SIGINT** or
**SIGTERM** to the child process (which by now will be the
**pressure-vessel-adverb** process that replaces **pressure-vessel-wrap**),
and to the process group that it leads (if any):

    assert (child_pid > 1);
    kill (child_pid, SIGTERM);
    kill (-child_pid, SIGTERM);

It must not use **SIGKILL**, because that cannot be forwarded across
the IPC connection.

## Once per command

For each command that is to be run as part of the game, Steam must
run **steam-runtime-launch-client**(1) with a **--socket**, **--dbus-address**
or **--bus-name** option that indicates how to communicate with the
launcher. Alternatively, it could do this itself by using D-Bus.

If Steam needs to set environment variables for the commands that are
run as part of the game, it can do so in one of two ways:

* If the environment variable is equally valid for all commands, it can
    be part of the environment of **steam-runtime-launcher-service**.
* If the environment variable is specific to one command, Steam can pass
    **--env VAR=VALUE** to **steam-runtime-launch-client**.

If the command is to be terminated without affecting other commands,
Steam should send **SIGINT** to **steam-runtime-launch-client**.
It must not use **SIGKILL**, because that cannot be forwarded across the
IPC connection. It should not use **SIGTERM** unless the entire game is
being terminated, because that's only sent to a single process and not
to an entire process group.

# EXAMPLES

Listen on the session bus, and run two commands, and exit:

    name="com.steampowered.PressureVessel.App${SteamAppId}"
    pressure-vessel-wrap \
        ... \
        -- \
        steam-runtime-launcher-service --bus-name "$name" &
    launcher_pid="$!"
    gdbus wait --session --bus-name "$name"

    steam-runtime-launch-client \
        --bus-name "$name" \
        -- \
        ls -al
    steam-runtime-launch-client \
        --bus-name "$name" \
        -- \
        id

    kill -TERM "$launcher_pid"
    wait "$launcher_pid"

Listen on a socket in a temporary directory, and use `--exit-with-fd`.
Production code would be more likely to invoke **steam-runtime-launch-client**
from C, C++ or Python code rather than a shell script, and use pipes
instead of fifos.

    tmpdir="$(mktemp -d)"
    mkfifo "${tmpdir}/exit-fifo"
    mkfifo "${tmpdir}/ready-fifo"
    pressure-vessel-wrap \
        --filesystem="${tmpdir}" \
        --pass-fd=3
        ... \
        -- \
        steam-runtime-launcher-service \
            --exit-on-readable=0 \
            --info-fd=3 \
            --socket="${tmpdir}/launcher" \
        < "${tmpdir}/exit-fifo" \
        3> "${tmpdir}/ready-fifo" \
        &
    launcher_pid="$!"
    sleep infinity > "${tmpdir}/exit-fifo" &
    sleep_pid="$!"
    # Wait for EOF so we know the socket is available
    cat "${tmpdir}/ready-fifo" > /dev/null

    steam-runtime-launch-client \
        --socket="${tmpdir}/launcher" \
        -- \
        ls -al
    steam-runtime-launch-client \
        --socket="${tmpdir}/launcher" \
        -- \
        id
    steam-runtime-launch-client \
        --socket="${tmpdir}/launcher" \
        -- \
        sleep infinity &
    launch_sleep_pid="$!"

    # Make steam-runtime-launcher-service's stdin become readable, which
    # will make it exit due to --exit-on-readable
    kill "$sleep_pid"
    wait "$launcher_pid" || echo "launcher exit status: $?"
    wait "$launch_sleep_pid" || echo "launched process exit status: $?"
    rm -fr "$tmpdir"

<!-- vim:set sw=4 sts=4 et: -->
