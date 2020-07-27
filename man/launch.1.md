---
title: pressure-vessel-launch
section: 1
...

# NAME

pressure-vessel-launch - client to launch processes in a container

# SYNOPSIS

**pressure-vessel-launch**
[**--clear-env**]
[**--directory** *DIR*]
[**--env** _VAR_**=**_VALUE_]
[**--forward-fd** *FD*]
[**--verbose**]
{**--bus-name** *NAME*|**--dbus-address** *ADDRESS*|**--socket** *SOCKET*}
[**--**]
*COMMAND* [*ARGUMENTS...*]

# DESCRIPTION

**pressure-vessel-launch** connects to an `AF_UNIX` socket established
by **pressure-vessel-launcher**(1), and executes an arbitrary command
as a subprocess of **pressure-vessel-launcher**.

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
    the given *NAME*, which is assumed to be owned by
    **pressure-vessel-launcher**.

**--clear-env**
:   The *COMMAND* runs in an empty environment, apart from any environment
    variables set by **--env**. By default, it inherits environment
    variables from **pressure-vessel-launcher**, with **--env**
    overriding individual variables.

**--directory** *DIR*
:   Arrange for the *COMMAND* to run in *DIR*.
    By default, it inherits the current working directory from
    **pressure-vessel-launcher**.

**--env** _VAR=VALUE_
:   Set environment variable _VAR_ to _VALUE_.
    This is mostly equivalent to using
    **env** _VAR=VALUE_ *COMMAND* *ARGUMENTS...*
    as the command.

**--forward-fd** *FD*
:   Arrange for the *COMMAND* to receive file descriptor number *FD*
    from outside the container. File descriptors 0, 1 and 2
    (standard input, standard output and standard error) are always
    forwarded.

**--verbose**
:   Be more verbose.

# OUTPUT

The standard output from *COMMAND* is printed on standard output.

The standard error from *COMMAND* is printed on standard error.
Diagnostic messages from **pressure-vessel-launch** may also be printed
on standard error.

# EXIT STATUS

The exit status is similar to **env**(1):

0
:   The *COMMAND* exited successfully with status 0.

125
:   Invalid arguments were given, or **pressure-vessel-launch** failed
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
    **pressure-vessel-launch** was disconnected from the D-Bus
    session bus or the **--socket** before the exit status could be
    determined.

Any value less than 128
:   The *COMMAND* exited unsuccessfully with the status indicated.

128 + *n*
:   The *COMMAND* was killed by signal *n*.

# EXAMPLES

See **pressure-vessel-launcher**(1).

<!-- vim:set sw=4 sts=4 et: -->
