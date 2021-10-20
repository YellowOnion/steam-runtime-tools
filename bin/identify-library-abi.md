---
title: steam-runtime-identify-library-abi
section: 1
...

<!-- This document:
Copyright 2021 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

# NAME

steam-runtime-identify-library-abi - Identify the ABI of the libraries stored in a specific directory or from the ldconfig output list

# SYNOPSIS

**steam-runtime-identify-library-abi**

# DESCRIPTION

# OPTIONS

**--directory** *DIR*
:   The list of libraries to identify is gathered by recursively search in *DIR*.

**--ldconfig**
:   Identify the ABI of the libraries listed by the executable `ldconfig`.

**--print0**
:   The generated library_path=library_ABI pairs are terminated with a null
    character instead of a newline.

**--skip-unversioned**
:   If a library filename ends with just `.so`, its ABI will not be identified
    and will not be printed in output.

**--version**
:   Instead of performing the libraries identification, write in output the
    version number as YAML.

# OUTPUT

**steam-runtime-identify-library-abi** standard output is machine parsable, with
pairs of `library_path=library_ABI` separated by a null character, with the option
**--print0**, or by newlines.
Where `library_ABI` follows the Debian-style multiarch tuples convention and
currently can have the following values: `i386-linux-gnu`, `x86_64-linux-gnu`,
`x86_64-linux-gnux32`, `aarch64-linux-gnu`, or `?` that groups all the other
possible ABIs.

# EXIT STATUS

0
:   Success.

64
:   Invalid arguments were given (EX_USAGE).

Other Nonzero
:   An error occurred.

<!-- vim:set sw=4 sts=4 et: -->
