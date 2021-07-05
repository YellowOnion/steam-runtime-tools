---
title: steam-runtime-urlopen
section: 1
...

<!-- This document:
Copyright 2021 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

# NAME

steam-runtime-urlopen - Alternative xdg-open executable

# SYNOPSIS

**steam-runtime-urlopen** [*OPTIONS*]... {*file* | *URL*}

# DESCRIPTION

**steam-runtime-urlopen** is an alternative xdg-open executable that provides
better handling for Steam URLs. This tool is expected to be executed from
inside a Steam Runtime container.

# OPTIONS

**--version**
:   Instead of opening the file/URL, write in output the version number.

# OUTPUT

On success, the output will be empty.

On error, a human-readable message is shown on standard error.

# EXIT STATUS

The exit status is intended to be the same as for **xdg-open**(1):

0
:   Success.

1
:   Error in command line syntax.

4
:   The action failed.

<!-- vim:set sw=4 sts=4 et: -->
