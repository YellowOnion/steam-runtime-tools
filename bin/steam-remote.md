---
title: steam-runtime-steam-remote
section: 1
...

<!-- This document:
Copyright 2021 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

# NAME

steam-runtime-steam-remote - stub Steam executable

# SYNOPSIS

**steam-runtime-steam-remote** [*ARGUMENT*]...

# DESCRIPTION

**steam-runtime-steam-remote** passes its command-line arguments
to the Steam client that is currently running, if any.

The command-line arguments will typically be a single
[steam: URL](https://developer.valvesoftware.com/wiki/Steam_browser_protocol)
such as `steam://advertise/70`, but can include other
[Steam command-line options](https://developer.valvesoftware.com/wiki/Command_Line_Options#Steam_.28Windows.29)
such as `-foreground`.

A typical use for **steam-runtime-steam-remote** is to add a symbolic
link named **steam** to a directory in the **PATH**, so that when a
game runs a command like `steam -foreground`, it will be converted into
an inter-process communication to the Steam client that is already running.

# OUTPUT

If the commands are correctly passed, the output will be empty.

On error, a human-readable message is shown on standard error.

# EXIT STATUS

0
:   **steam-runtime-steam-remote** ran successfully

Other Nonzero
:   An error occurred.

# EXAMPLES

Bring Steam's graphical user interface to the foreground:

    steam-runtime-steam-remote -foreground

Attempt to install Half-Life:

    steam-runtime-steam-remote steam://install/70


<!-- vim:set sw=4 sts=4 et: -->
