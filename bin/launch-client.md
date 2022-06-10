---
title: steam-runtime-launch-client
section: 1
...

<!-- This document:
Copyright © 2020-2021 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

# NAME

steam-runtime-launch-client - client to launch processes in a container

# SYNOPSIS

**steam-runtime-launch-client**
[**--clear-env**]
[**--directory** *DIR*]
[**--env** _VAR_**=**_VALUE_]
[**--forward-fd** *FD*]
[**--pass-env** *VAR*]
[**--pass-env-matching** *WILDCARD*]
[**--unset-env** *VAR*]
[**--verbose**]
{**--bus-name** *NAME*|**--dbus-address** *ADDRESS*|**--socket** *SOCKET*}
[**--**]
*COMMAND* [*ARGUMENTS...*]

**steam-runtime-launch-client**
[**--verbose**]
{**--bus-name** *NAME*|**--dbus-address** *ADDRESS*|**--socket** *SOCKET*}
**--terminate**

# DESCRIPTION

**steam-runtime-launch-client** connects to an `AF_UNIX` socket established
by **steam-runtime-launcher-service**(1), and executes an arbitrary command
as a subprocess of **steam-runtime-launcher-service**.

# OPTIONS

**--socket** *PATH*, **--socket** *@ABSTRACT*
:   Connect to the given `AF_UNIX` socket, which can either be an
    absolute path, or `@` followed by an arbitrary string.
    An absolute path indicates a filesystem-based socket, which is
    associated with the filesystem and can be shared between filesystem
    namespaces by bind-mounting.
    `@` indicates an an abstract socket, which is associated with a
    network namespace, is shared between all containers that are in
    the same network namespace, and cannot be shared across network
    namespace boundaries.

**--dbus-address** *ADDRESS*
:   The same as **--socket**, but the socket is specified in the form
    of a D-Bus address.

**--bus-name** *NAME*
:   Connect to the well-known D-Bus session bus and send commands to
    the given *NAME*, which is normally assumed to be owned by
    **steam-runtime-launcher-service**.

    As a special case, if the *NAME* is
    **org.freedesktop.Flatpak**, then it is assumed to be
    owned by the **flatpak-session-helper** per-user service, and the
    *COMMAND* is launched on the host system, similar to the
    **--host** option of **flatpak-spawn**(1).
    The **--terminate** option is not allowed in this mode.

    As another special case, if the *NAME* is
    **org.freedesktop.portal.Flatpak**, then it is assumed to be
    owned by the **flatpak-portal** per-user service, and the
    *COMMAND* is launched in a Flatpak sub-sandbox, similar to
    **flatpak-spawn**(1) without the **--host** option.
    Most options that alter how the sub-sandbox is created, such as
    **--sandbox-flag**, are not currently supported.
    As with **org.freedesktop.Flatpak**, the
    **--terminate** option is not allowed in this mode.

**--app-path** *PATH*, **--app-path=**
:   When creating a Flatpak subsandbox, mount *PATH* as the `/app` in
    the new subsandbox. If *PATH* is specified as the empty string,
    place an empty directory at `/app`.

    This experimental option is only valid when used with
    **--bus-name=org.freedesktop.portal.Flatpak**, and requires a
    Flatpak branch that has not yet been merged.

**--clear-env**
:   The *COMMAND* runs in an empty environment, apart from any environment
    variables set by **--env** and similar options, and environment
    variables such as `PWD` that are set programmatically (see
    **ENVIRONMENT** section, below).
    By default, it inherits environment variables from
    **steam-runtime-launcher-service**, with **--env** and
    similar options overriding or unsetting individual variables.

**--directory** *DIR*
:   Arrange for the *COMMAND* to run in *DIR*.
    By default, it inherits the current working directory from
    **steam-runtime-launcher-service**.

**--forward-fd** *FD*
:   Arrange for the *COMMAND* to receive file descriptor number *FD*
    from outside the container. File descriptors 0, 1 and 2
    (standard input, standard output and standard error) are always
    forwarded.

**--share-pids**
:   If used with **--bus-name=org.freedesktop.portal.Flatpak**, use the
    same process ID namespace for the new subsandbox as for the calling
    process. This requires Flatpak 1.10 or later, running on a host
    operating system where **bwrap**(1) is not and does not need to be
    setuid root.

    When communicating with a different API, this option is ignored.

**--terminate**
:   Instead of running a *COMMAND*, terminate the Launcher server.

**--usr-path** *PATH*
:   When creating a Flatpak subsandbox, mount *PATH* as the `/usr` in
    the new subsandbox.

    This experimental option is only valid when used with
    **--bus-name=org.freedesktop.portal.Flatpak**, and requires a
    Flatpak branch that has not yet been merged.

**--verbose**
:   Be more verbose.

# ENVIRONMENT OPTIONS

Options from this group are processed in order, with each option taking
precedence over any earlier options that affect the same environment variable.
For example,
**--pass-env-matching="FO&#x2a;" --env=FOO=bar --unset-env=FOCUS**
will set **FOO** to **bar**, unset **FOCUS** even if the caller has
it set, and pass through **FONTS** from the caller.

**--env** _VAR=VALUE_
:   Set environment variable _VAR_ to _VALUE_.
    This is mostly equivalent to using
    **env** _VAR=VALUE_ *COMMAND* *ARGUMENTS...*
    as the command.

**--pass-env** *VAR*
:   If the environment variable *VAR* is set, pass its current value
    into the container as if via **--env**. Otherwise, unset it as if
    via **--unset-env**.

**--pass-env-matching** *WILDCARD*
:   For each environment variable that is set and has a name matching
    the **fnmatch**(3) pattern *WILDCARD*, pass its current value
    into the container as if via **--env**.
    For example, **--pass-env-matching=Steam&#x2a;** copies Steam-related
    environment variables.
    If this command is run from a shell, the wildcard will usually need
    to be quoted, for example **--pass-env-matching="Steam&#x2a;"**.

**--unset-env** *VAR*
:   Unset *VAR* when running the command.
    This is mostly equivalent to using
    **env -u** *VAR* *COMMAND* *ARGUMENTS...*
    as the command.

# ENVIRONMENT

`PWD`
:   **steam-runtime-launcher-service**(1) sets this to the current working
    directory (as specified by **--directory**, or inherited from the
    launcher) for each command executed inside the container,
    overriding the environment options shown above.

`PRESSURE_VESSEL_LOG_INFO` (boolean)
:   If set to `1`, increase the log verbosity up to the info level.
    If set to `0`, no effect.

`PRESSURE_VESSEL_LOG_WITH_TIMESTAMP` (boolean)
:   If set to `1`, prepend the log entries with a timestamp.
    If set to `0`, no effect.

# OUTPUT

The standard output from *COMMAND* is printed on standard output.

The standard error from *COMMAND* is printed on standard error.
Diagnostic messages from **steam-runtime-launch-client** may also be printed
on standard error.

# EXIT STATUS

The exit status is similar to **env**(1):

0
:   The *COMMAND* exited successfully with status 0,
    or **--terminate** succeeded.

125
:   Invalid arguments were given, or **steam-runtime-launch-client** failed
    to start.

126
:   Reserved to indicate inability to launch the *COMMAND*.
    This is not currently distinguished from exit status 125.

127
:   Reserved to indicate that the *COMMAND* was not found.
    This is not currently distinguished from exit status 125.

128
:   The *COMMAND* was launched, but its exit status could not be
    determined. This happens if the wait-status was neither
    normal exit nor termination by a signal. It also happens if
    **steam-runtime-launch-client** was disconnected from the D-Bus
    session bus or the **--socket** before the exit status could be
    determined.

Any value less than 128
:   The *COMMAND* exited unsuccessfully with the status indicated.

128 + *n*
:   The *COMMAND* was killed by signal *n*.
    (This is the same encoding used by **bash**(1), **bwrap**(1) and
    **env**(1).)

# EXAMPLES

See **steam-runtime-launcher-service**(1).

<!-- vim:set sw=4 sts=4 et: -->
