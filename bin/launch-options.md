---
title: steam-runtime-launch-options
section: 1
...

<!-- This document:
Copyright 2019-2022 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

# NAME

steam-runtime-launch-options - alter the launch options of a Steam game

# SYNOPSIS

In Steam, set Launch Options to:

**steam-runtime-launch-options** [*OPTIONS*] **-- %command%**

# DESCRIPTION

**steam-runtime-launch-options** alters the options and compatibility
tools used to launch a game or other application. In particular, it
can switch between versions of the Steam Runtime.

# REQUIREMENTS

**steam-runtime-launch-options** requires Python 3.4 or later, with
GObject-Introspection bindings for GTK 3.

**launch-options.py** must either be installed as
**../libexec/steam-runtime-tools-0/launch-options.py** relative to
**launch-options.sh**, or installed in the same directory as
**launch-options.sh**, with the same name, removing any **.sh**
extension and appending **.py**.

# OPTIONS

**--verbose**
:   Show additional information.

# EXIT STATUS

The exit status is similar to **env**(1):

0
:   The *COMMAND* exited successfully with status 0.

125
:   Invalid arguments were given, or **steam-runtime-launch-options**
    failed to start.

126
:   Reserved to indicate inability to launch the *COMMAND*.
    This is not currently distinguished from exit status 125.

127
:   Reserved to indicate that the *COMMAND* was not found.
    This is not currently distinguished from exit status 125.

130
:   The launcher was interrupted with Ctrl+C.

Any value less than 128
:   The *COMMAND* exited unsuccessfully with the status indicated.

<!-- vim:set sw=4 sts=4 et: -->
