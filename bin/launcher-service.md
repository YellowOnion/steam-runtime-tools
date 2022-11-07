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
[**--exec-fallback**]
[**--exit-on-readable**] *FD*
[**--info-fd**] *N*
[**--replace**]
[**--[no-]stop-on-exit**]
[**--[no-]stop-on-name-loss**]
[**--verbose**]
{**--session**|**--bus-name** *NAME*...|**--socket** *SOCKET*|**--socket-directory** *PATH*}
[**--** *COMMAND* *ARGUMENTS*]

# DESCRIPTION

**steam-runtime-launcher-service** listens on an `AF_UNIX` socket or the
D-Bus session bus, and executes arbitrary commands as subprocesses.

If a *COMMAND* is specified, it is run as an additional subprocess.
It inherits standard input, standard output and standard error
from **steam-runtime-launcher-service** itself.

If the *COMMAND* exits, then the launcher will also exit (as though the
*COMMAND* had been started via **steam-runtime-launch-client --terminate**),
unless prevented by **--no-stop-on-exit** or
**SRT_LAUNCHER_SERVICE_STOP_ON_EXIT=0**.

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

    This option is deprecated. Using **--session** or **--bus-name** is
    preferred.

**--socket-directory** *PATH*
:   Create a filesystem-based socket with an arbitrary name in the
    given directory and listen on that. Clients can use the information
    printed on standard output to connect to the socket. Due to
    limitations of `AF_UNIX` socket addresses, the absolute path to the
    directory must be no more than 64 bytes. It must not contain ASCII
    control characters.

    This option is deprecated. Using **--session** or **--bus-name** is
    preferred.

**--bus-name** *NAME*
:   Connect to the well-known D-Bus session bus, request the given name
    and listen for commands there.
    This option may be used more than once.
    If it is, each of the names will be requested in the order given.
    If at least one name cannot be acquired or is subsequently lost,
    **steam-runtime-launcher-service** will behave according to the
    **--[no-]stop-on-exit** options.

**--exec-fallback**
:   If unable to set up the **--socket**, **--socket-directory**,
    **--bus-name** or **--session**, fall back to executing the
    *COMMAND* directly (replacing the **steam-runtime-launcher-service**
    process, similar to **env**(1)).
    This option is only allowed if a *COMMAND* is specified.
    It is useful if running the *COMMAND* is more important than the ability
    to insert additional commands into its execution environment.

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
    The other **steam-runtime-launcher-service** will either exit
    or continue to run, depending on whether the **--[no-]stop-on-name-loss**
    options were used.
    This option is ignored if **--bus-name** is not used.

**--session**
:   Equivalent to **--bus-name** *NAME*, but *NAME* is chosen automatically.
    The current implementation is that if a Steam app ID (game ID) can
    be discovered from the environment, then the *NAME* is
    **com.steampowered.App** followed by the app ID.
    Otherwise, **com.steampowered.App0** is used.
    Additionally, a second name of the form
    **com.steampowered.AppX.InstanceY** is generated, to allow multiple
    instances of the same app with **--no-stop-on-name-loss**.

    If both **--session** and **--bus-name** are used, then **--session**
    has no effect.

**--[no-]stop-on-exit**
:   With **--stop-on-exit** and a *COMMAND*, the server will terminate
    other launched processes and prepare to exit when the *COMMAND* exits.
    If other launched processes continue to run after receiving the
    **SIGTERM** signal, the server will still wait for them to exit
    before terminating.
    This is the default.

    With **--no-stop-on-name-loss** and a *COMMAND*, do not do this:
    the server will still be contactable via D-Bus using its unique bus name
    until it is terminated, for example with **SIGTERM** or
    **steam-runtime-launch-client --bus-name=:1.xx --terminate**.
    Note that if the wrapped *COMMAND* is a Steam game, then Steam will
    still consider the game to be running until the
    **steam-runtime-launcher-service** is terminated.

**--[no-]stop-on-name-loss**
:   With **--bus-name** and **--stop-on-name-loss**, the server will
    prepare to exit when any of the well-known bus names *NAME* used with
    **--bus-name** is lost, most likely because another server was
    run with **--replace** and took over ownership.
    If other launched processes are active, it will wait for them to exit
    before terminating.
    This is the default.

    With **--bus-name** and **--no-stop-on-name-loss**, do not do this:
    the server will still be contactable via D-Bus using its unique bus name.

    These parameters have no effect if **--bus-name** is not used.

**--verbose**
:   Be more verbose.

# ENVIRONMENT

## Variables set for the command

Some variables will be set programmatically in the subprocesses started
by **steam-runtime-launcher-service** when a command is sent to it by
**steam-runtime-launch-client**:

`MAINPID`
:   If **steam-runtime-launcher-service** was run as a wrapper around a
    *COMMAND* (for example as
    **steam-runtime-launcher-service --bus-name=... -- my-game**),
    and the initial process of the wrapped *COMMAND* is still running,
    then this variable is set to its process ID (for example, the process
    ID of **my-game**). Otherwise, this variable is cleared.

`PWD`
:   Set to the current working directory for each command executed
    inside the container.

Additionally, **steam-runtime-launch-client** has several options that
manipulate environment variables on a per-command basis.

## Variables read by steam-runtime-launcher-service

`PRESSURE_VESSEL_LOG_INFO` (boolean)
:   If set to 1, same as `SRT_LOG=info`

`PRESSURE_VESSEL_LOG_WITH_TIMESTAMP` (boolean)
:   If set to 1, same as `SRT_LOG=timestamp`

`SRT_LAUNCHER_SERVICE_STOP_ON_EXIT` (boolean)
:   If set to `0`, the default behaviour changes to be equivalent to
    **--no-stop-on-exit**, unless overridden by **--stop-on-exit**.
    If set to `1`, no effect.

`SRT_LAUNCHER_SERVICE_STOP_ON_NAME_LOSS` (boolean)
:   If set to `0`, the default behaviour changes to be equivalent to
    **--no-stop-on-name-loss**, unless overridden by **--stop-on-name-loss**.
    If set to `1`, no effect.

`SRT_LOG`
:   A sequence of tokens separated by colons, spaces or commas
    affecting how output is recorded. See source code for details.

`STEAM_COMPAT_APP_ID` (integer)
:   Used by **--session** to identify the Steam app ID (game ID).

`SteamAppId` (integer)
:   Used by **--session** to identify the Steam app ID (game ID),
    if `STEAM_COMPAT_APP_ID` is not also set.

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

# EXAMPLE

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

<!-- vim:set sw=4 sts=4 et: -->
