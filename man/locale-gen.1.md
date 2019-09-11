---
title: pressure-vessel-locale-gen
section: 1
...

# NAME

pressure-vessel-locale-gen - generate additional locales

# SYNOPSIS

**pressure-vessel-locale-gen** [*LOCALE*…]

# DESCRIPTION

This tool generates locale files for the locales that might be necessary
for Steam games. It should be invoked with the current working directory
set to an empty directory.

If the current working directory is non-empty, the behaviour is undefined.
Existing subdirectories corresponding to locales might be overwritten, or
might be kept. (The current implementation is that they are kept, even if
they are outdated or invalid.)

# OPTIONS

**--verbose**
:   Be more verbose.

# POSITIONAL ARGUMENTS

*LOCALE*
:   One or more additional POSIX locale names, such as **en_US.UTF-8** or
    **it_IT@euro**. By default, **pressure-vessel-locale-gen**
    generates all the locales required by the standard locale environment
    variables such as **LC_ALL**, plus the value of *$HOST_LC_ALL* if
    set, plus the American English locale **en_US.UTF-8** (which is
    frequently assumed to be present, even though many operating systems
    do not guarantee it).

# OUTPUT

The diagnostic output on standard error is not machine-readable.

Locale files are generated in the current working directory. On exit,
if the current working directory is non-empty (exit status 72 or 73),
its path should be added to the **LOCPATH** environment variable. If
the current working directory is empty (exit status 0),
**LOCPATH** should not be modified.

# EXIT STATUS

0
:   All necessary locales were already available without altering
    the **LOCPATH** environment variable.

64 (**EX_USAGE** from **sysexits.h**)
:   Invalid arguments were given.

72 (**EX_OSFILE**)
:   Not all of the necessary locales were already available, but all
    were generated in the current working directory.

73 (**EX_CANTCREAT**)
:   At least one locale was not generated successfully. Some other
    locales might have been generated in the current working directory
    as usual.

78 (**EX_CONFIG**)
:   One of the locales given was invalid or could lead to path traversal.
    Other locales might have been generated as usual.

Anything else
:   There was an internal error.

# EXAMPLE

    $ mkdir locales
    $ ( cd locales; pressure-vessel-locale-gen )
    $ if [ $? = 0 ]; then \
      ./bin/some-game; \
    else \
      LOCPATH="$(pwd)/locales" ./bin/some-game; \
    fi

<!-- vim:set sw=4 sts=4 et: -->
