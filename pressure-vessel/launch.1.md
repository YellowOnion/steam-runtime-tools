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
[**--pass-env** *VAR*]
[**--pass-env-matching** *WILDCARD*]
[**--unset-env** *VAR*]
[**--verbose**]
{**--bus-name** *NAME*|**--dbus-address** *ADDRESS*|**--socket** *SOCKET*}
[**--**]
*COMMAND* [*ARGUMENTS...*]

**pressure-vessel-launch**
[**--verbose**]
{**--bus-name** *NAME*|**--dbus-address** *ADDRESS*|**--socket** *SOCKET*}
**--terminate**

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
    the given *NAME*, which is normally assumed to be owned by
    **pressure-vessel-launcher**.

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
    Options that alter how the sub-sandbox is created, such as
    **--sandbox-flag**, are not currently supported.
    As with **org.freedesktop.Flatpak**, the
    **--terminate** option is not allowed in this mode.

**--clear-env**
:   The *COMMAND* runs in an empty environment, apart from any environment
    variables set by **--env** and similar options.
    By default, it inherits environment variables from
    **pressure-vessel-launcher**, with **--env** and
    similar options overriding or unsetting individual variables.

**--directory** *DIR*
:   Arrange for the *COMMAND* to run in *DIR*.
    By default, it inherits the current working directory from
    **pressure-vessel-launcher**.

**--forward-fd** *FD*
:   Arrange for the *COMMAND* to receive file descriptor number *FD*
    from outside the container. File descriptors 0, 1 and 2
    (standard input, standard output and standard error) are always
    forwarded.

**--terminate**
:   Instead of running a *COMMAND*, terminate the Launcher server.

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
:   **pressure-vessel-launcher**(1) sets this to the current working
    directory (as specified by **--directory**, or inherited from the
    launcher) for each command executed inside the container,
    overriding the environment options shown above.

# OUTPUT

The standard output from *COMMAND* is printed on standard output.

The standard error from *COMMAND* is printed on standard error.
Diagnostic messages from **pressure-vessel-launch** may also be printed
on standard error.

# EXIT STATUS

The exit status is similar to **env**(1):

0
:   The *COMMAND* exited successfully with status 0,
    or **--terminate** succeeded.

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
    (This is the same encoding used by **bash**(1), **bwrap**(1) and
    **env**(1).)

# EXAMPLES

See **pressure-vessel-launcher**(1).

<!-- vim:set sw=4 sts=4 et: -->
