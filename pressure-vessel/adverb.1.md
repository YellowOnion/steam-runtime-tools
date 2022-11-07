---
title: pressure-vessel-adverb
section: 1
...

<!-- This document:
Copyright © 2020-2021 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

# NAME

pressure-vessel-adverb - wrap processes in various ways

# SYNOPSIS

**pressure-vessel-adverb**
[**--[no-]exit-with-parent**]
[**--fd** *FD*...]
[**--[no-]generate-locales**]
[**--ld-audit** *MODULE*[**:arch=***TUPLE*]...]
[**--ld-preload** *MODULE*[**:**...]...]
[**--pass-fd** *FD*...]
[[**--add-ld.so-path** *PATH*...]
**--regenerate-ld.so-cache** *PATH*]
[**--set-ld-library-path** *VALUE*]
[**--shell** **none**|**after**|**fail**|**instead**]
[**--subreaper**]
[**--terminal** **none**|**auto**|**tty**|**xterm**]
[**--terminate-timeout** *SECONDS* [**--terminate-idle-timeout** *SECONDS*]]
[[**--[no-]create**]
[**--[no-]wait**]
[**--[no-]write**]
**--lock-file** *FILENAME*...]
[**--verbose**]
[**--**]
*COMMAND* [*ARGUMENTS...*]

# DESCRIPTION

**pressure-vessel-adverb** runs *COMMAND* as a child process, with
modifications to its execution environment as determined by the options.

By default, it just runs *COMMAND* as a child process and reports its
exit status.

# OPTIONS

**--add-ld.so-path** *PATH*
:   Add *PATH* to the search path for **--regenerate-ld.so-cache**.
    The final search path will consist of all **--add-ld.so-path**
    arguments in the order they are given, followed by the lines
    from `runtime-ld.so.conf` in order.

**--create**
:   Create each **--lock-file** that appears on the command-line after
    this option if it does not exist, until a **--no-create** option
    is seen. **--no-create** reverses this behaviour, and is the default.

**--exit-with-parent**
:   Arrange for **pressure-vessel-adverb** to receive **SIGTERM**
    (which it will pass on to *COMMAND*, if possible) when its parent
    process exits. This is particularly useful when it is wrapped in
    **bwrap** by **pressure-vessel-wrap**, to arrange for the
    **pressure-vessel-adverb** command to exit when **bwrap** is killed.
    **--no-exit-with-parent** disables this behaviour, and is the default.

**--fd** *FD*
:   Receive file descriptor *FD* (specified as a small positive integer)
    from the parent process, and keep it open until
    **pressure-vessel-adverb** exits. This is most useful if *FD*
    is locked with a Linux open file description lock (**F_OFD_SETLK**
    or **F_OFD_SETLKW** from **fcntl**(2)), in which case the lock will
    be held by **pressure-vessel-adverb**.

**--generate-locales**
:   If not all configured locales are available, generate them in a
    temporary directory which is passed to the *COMMAND* in the
    **LOCPATH** environment variable.
    **--no-generate-locales** disables this behaviour, and is the default.

**--ld-audit** *MODULE*[**:arch=***TUPLE*]
:   Add *MODULE* to **LD_AUDIT** before executing *COMMAND*.
    The optional *TUPLE* is the same as for **--ld-preload**, below.

**--ld-preload** *MODULE*[**:arch=***TUPLE*]
:   Add *MODULE* to **LD_PRELOAD** before executing *COMMAND*.

    If the optional **:arch=***TUPLE* is given, the *MODULE* is only used for
    the given architecture, and is paired with other modules (if any) that
    share its basename; for example,
    `/home/me/.steam/root/ubuntu12_32/gameoverlayrenderer.so:arch=i386-linux-gnu`
    and
    `/home/me/.steam/root/ubuntu12_64/gameoverlayrenderer.so:arch=x86_64-linux-gnu`
    will be combined into a single **LD_PRELOAD** entry of the form
    `/tmp/pressure-vessel-libs-123456/${PLATFORM}/gameoverlayrenderer.so`.

    For a **LD_PRELOAD** module named `gameoverlayrenderer.so` in a directory
    named `ubuntu12_32` or `ubuntu12_64`, the architecture is automatically
    set to `i386-linux-gnu` or `x86_64-linux-gnu` respectively, if not
    otherwise given. Other special-case behaviour might be added in future
    if required.

**--lock-file** *FILENAME*
:   Lock the file *FILENAME* according to the most recently seen
    **--[no-]create**, **--[no-]wait** and **--[no-]write** options,
    using a Linux open file description lock (**F_OFD_SETLK** or
    **F_OFD_SETLKW** from **fcntl**(2)) if possible, or a POSIX
    process-associated record lock (**F_SETLK** or **F_SETLKW**) on older
    kernels.

**--pass-fd** *FD*
:   Pass the file descriptor *FD* (specified as a small positive integer)
    from the parent process to the *COMMAND*. The default is to only pass
    through file descriptors 0, 1 and 2
    (**stdin**, **stdout** and **stderr**).

**--regenerate-ld.so-cache** *PATH*
:   Regenerate "ld.so.cache" in the directory *PATH*.

    On entry to **pressure-vessel-adverb**, *PATH* should
    contain `runtime-ld.so.conf`, a symbolic link or copy
    of the runtime's original `/etc/ld.so.conf`. It will
    usually also contain `ld.so.conf` and `ld.so.cache`
    as symbolic links or copies of the runtime's original
    `/etc/ld.so.conf` and `/etc/ld.so.cache`.

    Before executing the *COMMAND*, **pressure-vessel-adverb**
    will construct a new `ld.so.conf` in *PATH*, consisting of
    all **--add-ld.so-path** arguments, followed by the contents
    of `runtime-ld.so.conf`; then it will generate a new
    `ld.so.cache` from that configuration. Both of these
    will atomically replace the original files in *PATH*.

    Other filenames in *PATH* will be used temporarily.

    To make use of this feature, a container's `/etc/ld.so.conf`
    and `/etc/ld.so.cache` should usually be symbolic links into
    the *PATH* used here, which will typically be below `/run`.

**--set-ld-library-path** *VALUE*
:   Set the environment variable LD_LIBRARY_PATH to *VALUE* after
    processing **--regenerate-ld.so-cache** (if used), but before
    executing *COMMAND*.

**--shell=after**
:   Run an interactive shell after *COMMAND* exits.
    In that shell, running **"$@"** will re-run *COMMAND*.

**--shell=fail**
:   The same as **--shell=after**, but do not run the shell if *COMMAND*
    exits with status 0.

**--shell=instead**
:   The same as **--shell=after**, but do not run *COMMAND* at all.

**--shell=none**
:   Don't run an interactive shell. This is the default.

**--subreaper**
:   If the *COMMAND* starts background processes, arrange for them to
    be reparented to **pressure-vessel-adverb** instead of to **init**
    when their parent process exits, and do not exit until all such
    descendant processes have exited.
    A non-negative **--terminate-timeout** implies this option.

**--terminal=auto**
:   Equivalent to **--terminal=xterm** if a **--shell** option other
    than **none** is used, or **--terminal=none** otherwise.
    This is the default.

**--terminal=none**
:   Disable features that would ordinarily use a terminal.

**--terminal=tty**
:   Use the current execution environment's controlling tty for
    the **--shell** options.

**--terminal=xterm**
:   Start an **xterm**(1), and run *COMMAND* and/or an interactive
    shell in that environment.

**--terminate-idle-timeout** *SECONDS*
:   If a non-negative **--terminate-timeout** is specified, wait this
    many seconds before sending **SIGTERM** to child processes.
    Non-integer decimal values are allowed.
    0 or negative means send **SIGTERM** immediately, which is the
    default.
    This option is ignored if **--terminate-timeout** is not used.

**--terminate-timeout** *SECONDS*
:   If non-negative, terminate background processes after the *COMMAND*
    exits. This implies **--subreaper**.
    Non-integer decimal values are allowed.
    When this option is enabled, after *COMMAND* exits,
    **pressure-vessel-adverb** will wait for up to the time specified
    by **--terminate-idle-timeout**, then send **SIGTERM** to any
    remaining child processes and wait for them to exit gracefully.
    If child processes continue to run after a further time specified
    by this option, send **SIGKILL** to force them to be terminated,
    and continue to send **SIGKILL** until there are no more descendant
    processes. If *SECONDS* is 0, **SIGKILL** is sent immediately.
    A negative number means signals are not sent, which is the default.

**--verbose**
:   Be more verbose.

**--wait**
:   For each **--lock-file** that appears on the command-line after
    this option until a **--no-wait** option is seen, if the file is
    already locked in an incompatible way, **pressure-vessel-adverb**
    will wait for the current holder of the lock to release it.
    With **--no-wait**, which is the default, **pressure-vessel-adverb**
    will exit with status 75 (**EX_TEMPFAIL**) if a lock cannot be acquired.

**--write**
:   Each **--lock-file** that appears on the command-line after
    this option will be locked in **F_WRLCK** mode (an exclusive/write
    lock), until a **--no-write** option is seen. **--no-write** results
    in use of **F_RDLCK** (a shared/read lock), and is the default.

# ENVIRONMENT

`PRESSURE_VESSEL_LOG_INFO` (boolean)
:   If set to 1, same as `SRT_LOG=info`

`PRESSURE_VESSEL_LOG_WITH_TIMESTAMP` (boolean)
:   If set to 1, same as `SRT_LOG=timestamp`

`SRT_LOG`
:   A sequence of tokens separated by colons, spaces or commas
    affecting how output is recorded. See source code for details.

# OUTPUT

The standard output from *COMMAND* is printed on standard output.

The standard error from *COMMAND* is printed on standard error.
Diagnostic messages from **pressure-vessel-adverb** may also be printed
on standard error.

# SIGNALS

If **pressure-vessel-adverb** receives signals **SIGHUP**, **SIGINT**,
**SIGQUIT**, **SIGTERM**, **SIGUSR1** or **SIGUSR2**, it immediately
sends the same signal to *COMMAND*, hopefully causing *COMMAND* to
exit gracefully.

# EXIT STATUS

Nonzero (failure) exit statuses are subject to change, and might be
changed to be more like **env**(1) in future.

0
:   The *COMMAND* exited successfully with status 0

1
:   An error occurred while setting up the execution environment or
    starting the *COMMAND*

64 (**EX_USAGE**)
:   Invalid arguments were given

69 (**EX_UNAVAILABLE**)
:   An error occurred while setting up the execution environment

70 (**EX_SOFTWARE**)
:   The *COMMAND* terminated in an unknown way

75 (**EX_TEMPFAIL**)
:   A **--lock-file** could not be acquired, and **--wait** was not given

127
:   The *COMMAND* could not be started

Any value 1-255
:   The *COMMAND* exited unsuccessfully with the status indicated

128 + *n*
:   The *COMMAND* was killed by signal *n*
    (this is the same encoding used by **bash**(1), **bwrap**(1) and
    **env**(1))

# EXAMPLE

When running a game in a container as a single command,
**pressure-vessel-wrap** uses a pattern similar to:

    bwrap \
        ... \
    /path/to/pressure-vessel-adverb \
        --exit-with-parent \
        --generate-locales \
        --lock-file=/path/to/runtime/.ref \
        --subreaper \
        -- \
    /path/to/game ...

to preserve Steam's traditional behaviour for native Linux games (where
all background processes must exit before the game is considered to have
exited).

When running **steam-runtime-launcher-service** in a container, the adverb looks
more like this:

    bwrap \
        ... \
    /path/to/pressure-vessel-adverb \
        --exit-with-parent \
        --generate-locales \
        --lock-file=/path/to/runtime/.ref \
        --subreaper \
        --terminate-timeout=10 \
        -- \
    /path/to/steam-runtime-launcher-service ...

so that when the **steam-runtime-launcher-service** is terminated by
**steam-runtime-launch-client --terminate**, or when the **bwrap** process
receives a fatal signal, the **pressure-vessel-adverb** process will
gracefully terminate any remaining child/descendant processes before
exiting itself.

<!-- vim:set sw=4 sts=4 et: -->
