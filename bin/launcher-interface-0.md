---
title: steam-runtime-launcher-interface-0
section: 1
...

<!-- This document:
Copyright © 2020-2022 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

# NAME

steam-runtime-launcher-interface-0 - entry point for steam-runtime-launcher-service

# SYNOPSIS

**steam-runtime-launcher-interface-0**
*TOOL NAME*[**:**_TOOL NAME_...]
*COMMAND* [*ARGUMENTS*]

# DESCRIPTION

**steam-runtime-launcher-interface-0** runs an arbitrary command and
automatically detects whether to wrap the
**steam-runtime-launcher-service**(1) helper service around it.

It is intended to be run by Steam compatibility tools such as Proton
and the Steam container runtime.
Steam can request that a particular compatibility tool gets an associated
**steam-runtime-launcher-service**(1) by setting the
**STEAM_COMPAT_LAUNCHER_SERVICE** environment variable.

At least two positional parameters are required.

The first parameter is a tool name to be matched against the environment
variable set by Steam, or a **:**-separated list of alternative names
for the same tool.
It must not start with **-**, which is reserved for possible future use.
If more than one name is listed, they are treated as equivalent names for
the same compatibility tool, and the command server will be activated
if Steam requests any of the given names: for example,
`scout-in-container:scout-on-soldier:1070560` could all be used as names
for the "Steam Linux Runtime" compatibility tool.

If the environment variable does not match any of the specified tool
names, **steam-runtime-launcher-interface-0** runs the *COMMAND*
with no further modification.

If the environment variable matches any of the specified tool names,
then **steam-runtime-launcher-interface-0** runs
**steam-runtime-launcher-service**(1) as a wrapper around the *COMMAND*.
Debugging commands can be run in the same execution environment
as the *COMMAND* by passing them to **steam-runtime-launch-client**(1).

This is a versioned interface.
If incompatible behaviour changes are made in a future
version of the Steam Runtime, the tool should be renamed to
**steam-runtime-launcher-interface-1**.

# ENVIRONMENT

`STEAM_COMPAT_LAUNCHER_SERVICE` (string)
:   A tool name which is matched against the first parameter.

`SRT_LAUNCHER_SERVICE_STOP_ON_EXIT` (boolean)
:   By default, or if set to `1`, the **steam-runtime-launcher-service**(1)
    will terminate other launched processes and prepare to exit when
    the *COMMAND* does.
    This is appropriate when interacting with a game or other program
    that is mostly working: it cleans up any associated debugging
    processes when the game itself exits, so that Steam will correctly
    conside the game to have exited.

    If set to `0`, the **steam-runtime-launcher-service**(1) will not
    stop until it is explicitly terminated by **SIGTERM** or
    **steam-runtime-launch-client --bus-name=... --terminate**.
    This is particularly useful when debugging a game that crashes or
    otherwise exits on startup.
    As long as the **steam-runtime-launcher-service**(1) continues
    to run, Steam will behave as though the game was still running:
    the user is responsible for terminating the service when it is no
    longer needed.

    This is a generic **steam-runtime-launcher-service**(1) option,
    and is not specific to **steam-runtime-launcher-interface-0**.

`SRT_LAUNCHER_SERVICE_STOP_ON_NAME_LOSS` (ignored)
:   Ignored.
    For robustness, the **steam-runtime-launcher-service**(1) run from
    this tool always behaves as though this variable is set to `0`.
    Even if its D-Bus well-known name is taken over by another process,
    it can still be contacted via its D-Bus unique name.

# OUTPUT

Unstructured diagnostic messages are printed on standard error.

The *COMMAND* inherits standard output and standard error.

# EXIT STATUS

0
:   The *COMMAND* exited successfully

125
:   Internal error in **steam-runtime-launcher-interface-0**,
    or incorrect command-line options

126
:   The *COMMAND* could not be started

Any nonzero status
:   The *COMMAND* exited unsuccessfully

# EXAMPLE

Please see **steam-runtime-launch-client**(1).

<!-- vim:set sw=4 sts=4 et: -->
